/* Frame-owned Vulkan palette uploads for the inherited Ranger VRIK solve. */
#include "quakedef.h"
#include "r_avatar.h"
#include "custom_avatar.h"
#include "r_vrik.h"
#include "r_vrik_render.h"
#include "vr_input.h"
#include "mem.h"

#include <float.h>
#include <limits.h>
#include <math.h>

extern cvar_t r_lerpmodels;
extern cvar_t r_gpulightmapupdate, r_rtshadows;
extern VkAccelerationStructureKHR bmodel_tlas;

#define R_VRIK_RENDER_MAX_JOINTS 256
#define R_VRIK_RENDER_MAX_AVATARS (PLAYER_AVATAR_COUNT + CUSTOM_AVATAR_MAX_PACKAGES)
#define R_VRIK_RENDER_FRAME_CPU_BUDGET (8u * 1024u * 1024u)

cvar_t r_avatar_humanoid = {"r_avatar_humanoid", "0", CVAR_ARCHIVE};

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
	float tracked_root_yaw;
	qboolean tracked_root_valid;
	double tracked_cull_local_bound;
	vec3_t tracked_cull_origin;
	qboolean tracked_cull_valid;
	float target_to_canonical[12];
	qboolean alternate_avatar;
	const aliashdr_t *attached_prop_geometry;
	float attached_prop_to_canonical[12];
	double attached_prop_local_bound;
	qboolean attached_prop_valid;
	r_vrik_prepared_muzzle_t muzzle;
} r_vrik_candidate_t;

typedef struct r_vrik_staged_avatar_s
{
	const entity_t *entity;
	const qmodel_t *original_model;
	qmodel_t *source_model;
	const aliashdr_t *source_geometry;
	qmodel_t *target_model;
	const aliashdr_t *target_geometry;
	const md5_skeleton_data_t *source_skeleton_data;
	const md5_skeleton_data_t *target_skeleton_data;
	md5_skeleton_view_t source_skeleton;
	md5_skeleton_view_t target_skeleton;
	r_avatar_rig_t source_rig;
	r_avatar_rig_t target_rig;
	const r_avatar_profile_t *base_profile;
	r_avatar_profile_t normalized_profile;
	r_avatar_humanoid_t humanoid_map;
	r_avatar_presentation_context_t presentation;
	const r_avatar_retarget_binds_t *retarget_binds;
	int id;
	float floor_correction_z;
	float target_to_canonical[12];
	qboolean humanoid;
	qboolean humanoid_ik;
	qboolean implicit_qbj3;
	qboolean valid;
} r_vrik_staged_avatar_t;

typedef struct r_vrik_frame_entry_s
{
	const entity_t *entity;
	int owner_slot;
	int selected_id;
	qboolean corpse;
	qboolean qbj3_death;
	qboolean custom_key_empty;
	qboolean selection_valid;
	r_vrik_staged_avatar_t selection;
	r_vrik_candidate_t candidate;
	float palette[R_VRIK_RENDER_MAX_JOINTS][12];
	r_vrik_prepared_palette_t prepared[DOUBLE_BUFFERED];
} r_vrik_frame_entry_t;

typedef struct r_vrik_frame_entries_s
{
	r_vrik_frame_entry_t *entries;
	size_t capacity;
	size_t count;
	size_t prepared_count[DOUBLE_BUFFERED];
} r_vrik_frame_entries_t;

typedef struct r_vrik_floor_cache_s
{
	const qmodel_t *source_model, *target_model;
	const md5_skeleton_data_t *source_skeleton, *target_skeleton;
	const r_avatar_profile_t *base_profile;
	float display_scale;
	float correction_z;
	qboolean attempted, valid;
} r_vrik_floor_cache_t;

/* The semantic names and skeleton hierarchy do not change between frames.
 * Keep only model-owned identity and resolved joint indexes here; each staged
 * player still receives its own live skeleton views and pose palette. */
typedef struct r_vrik_rig_cache_s
{
	const qmodel_t *source_model, *target_model;
	const md5_skeleton_data_t *source_skeleton, *target_skeleton;
	const r_avatar_profile_t *profile;
	r_avatar_rig_t source_rig, target_rig;
	r_avatar_retarget_binds_t retarget_binds;
	r_avatar_presentation_context_t generic_presentation, humanoid_presentation;
	r_avatar_humanoid_t humanoid_map;
	float humanoid_display_scale;
	qboolean generic_attempted, generic_valid;
	qboolean humanoid_attempted, humanoid_valid;
	qboolean valid;
} r_vrik_rig_cache_t;

static r_vrik_frame_entries_t frame_entries;
static qmodel_t *builtin_models[PLAYER_AVATAR_COUNT];
static qboolean builtin_attempted[PLAYER_AVATAR_COUNT];
static qmodel_t *custom_models[CUSTOM_AVATAR_MAX_PACKAGES];
static qboolean custom_attempted[CUSTOM_AVATAR_MAX_PACKAGES];
static r_vrik_floor_cache_t floor_cache[R_VRIK_RENDER_MAX_AVATARS][2];
static r_vrik_rig_cache_t rig_cache[R_VRIK_RENDER_MAX_AVATARS];
static char admission_gamedir[MAX_OSPATH];
static VkDescriptorSet palette_descriptor_sets[DOUBLE_BUFFERED];
static uint32_t active_frame_slot;
static qboolean active_frame_valid;

void R_VRIKRenderInvalidatePublication (void)
{
	active_frame_valid = false;
	frame_entries.count = 0;
	memset (frame_entries.prepared_count, 0, sizeof (frame_entries.prepared_count));
}

static void R_VRIKRenderResetModelCaches (void)
{
	memset (builtin_models, 0, sizeof (builtin_models));
	memset (builtin_attempted, 0, sizeof (builtin_attempted));
	memset (custom_models, 0, sizeof (custom_models));
	memset (custom_attempted, 0, sizeof (custom_attempted));
	memset (floor_cache, 0, sizeof (floor_cache));
	memset (rig_cache, 0, sizeof (rig_cache));
	q_strlcpy (admission_gamedir, com_gamedir, sizeof (admission_gamedir));
}

void R_VRIKRenderResetAdmission (void)
{
	R_VRIKRenderInvalidatePublication ();
	R_VRIKRenderResetModelCaches ();
}

static qboolean R_VRIKRenderReserveEntries (size_t required)
{
	const size_t max_capacity = R_VRIK_RENDER_FRAME_CPU_BUDGET / sizeof (*frame_entries.entries);
	size_t capacity, bytes;
	r_vrik_frame_entry_t *entries;

	if (required > max_capacity)
		required = max_capacity;
	if (required > SIZE_MAX / sizeof (*frame_entries.entries))
		return false;
	if (required <= frame_entries.capacity)
		return true;
	capacity = frame_entries.capacity ? frame_entries.capacity : 1;
	while (capacity < required)
	{
		if (capacity > max_capacity / 2)
		{
			capacity = max_capacity;
			break;
		}
		capacity *= 2;
	}
	if (capacity < required || capacity > SIZE_MAX / sizeof (*frame_entries.entries))
		return false;
	bytes = capacity * sizeof (*frame_entries.entries);
	if (bytes > R_VRIK_RENDER_FRAME_CPU_BUDGET)
		return false;
	entries = (r_vrik_frame_entry_t *)Mem_Realloc (frame_entries.entries, bytes);
	if (!entries)
		return false;
	frame_entries.entries = entries;
	frame_entries.capacity = capacity;
	return true;
}

static qmodel_t *R_VRIKRenderBuiltinModel (int id)
{
	if (strcmp (admission_gamedir, com_gamedir))
		R_VRIKRenderResetModelCaches ();
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

static qboolean R_VRIKRenderQBJ3Game (void)
{
	const char *game = COM_SkipPath (com_gamedir);
	return game && !q_strcasecmp (game, "qbj3");
}

static qboolean R_VRIKRenderQBJ3PlayerFrame (const entity_t *entity)
{
	return entity && entity->model && !entity->model->needload &&
		entity->model->type == mod_alias &&
		!q_strcasecmp (entity->model->name, "progs/player_qbj.mdl") &&
		entity->model->numframes == 143 && entity->frame >= 0 && entity->frame <= 142;
}

static qboolean R_VRIKRenderQBJ3LivePlayer (const entity_t *entity)
{
	return R_VRIKRenderQBJ3PlayerFrame (entity) &&
		!(entity->frame >= 41 && entity->frame <= 102);
}

static qboolean R_VRIKRenderQBJ3Death (const entity_t *entity)
{
	return R_VRIKRenderQBJ3PlayerFrame (entity) &&
		entity->frame >= 41 && entity->frame <= 102;
}

qboolean R_VRIKRenderOriginalModelEligible (const entity_t *entity)
{
	if (!entity || !entity->model || entity->model->needload ||
		entity->model->type != mod_alias)
		return false;
	if (R_VRIKRenderQBJ3Game ())
		return R_VRIKRenderQBJ3PlayerFrame (entity);
	return !strcmp (entity->model->name, "progs/player.mdl");
}

static qmodel_t *R_VRIKRenderCustomModel (int id)
{
	const int index = id - PLAYER_AVATAR_COUNT;
	qmodel_t *model;
	if (index < 0 || index >= CUSTOM_AVATAR_MAX_PACKAGES || !CustomAvatar_Get (id))
		return NULL;
	if (strcmp (admission_gamedir, com_gamedir))
		R_VRIKRenderResetModelCaches ();
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
static qboolean R_VRIKRenderReferenceVertex (const md5_avatar_bind_surface_t *surface,
	unsigned short vertex_index, const float (*reference)[12], size_t joint_count,
	double out[3])
{
	const md5_avatar_bind_vertex_skin_t *skin;
	if (!surface || !surface->skin || !surface->weights || !reference || !out ||
		vertex_index >= surface->numverts || joint_count == 0 ||
		joint_count > R_VRIK_RENDER_MAX_JOINTS)
		return false;
	skin = &surface->skin[vertex_index];
	if (!skin->numweights || skin->firstweight > surface->numweights ||
		skin->numweights > surface->numweights - skin->firstweight)
		return false;
	out[0] = out[1] = out[2] = 0.0;
	for (size_t influence = 0; influence < skin->numweights; ++influence)
	{
		const md5_avatar_bind_weight_t *weight =
			&surface->weights[skin->firstweight + influence];
		if (weight->joint_index >= joint_count)
			return false;
		const float *matrix = reference[weight->joint_index];
		for (int axis = 0; axis < 3; ++axis)
			out[axis] += (double)matrix[axis * 4] * weight->pos[0] +
				(double)matrix[axis * 4 + 1] * weight->pos[1] +
				(double)matrix[axis * 4 + 2] * weight->pos[2] +
				(double)matrix[axis * 4 + 3] * weight->pos[3];
	}
	return isfinite(out[0]) && isfinite(out[1]) && isfinite(out[2]);
}

static qboolean R_VRIKRenderMinimumBindZ (const qmodel_t *model,
	const r_avatar_profile_t *profile, qboolean source, qboolean contacts,
	const float target_to_canonical[12], const float (*reference)[12],
	size_t reference_joint_count, double *minimum_out)
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
				double point_xyz[3] = {v->xyz[0], v->xyz[1], v->xyz[2]};
				if (reference && !R_VRIKRenderReferenceVertex (surface,
					surface->indexes[index + point], reference,
					reference_joint_count, point_xyz))
					return false;
				const double z = source ? point_xyz[2] :
					(double)target_to_canonical[8] * point_xyz[0] +
					(double)target_to_canonical[9] * point_xyz[1] +
					(double)target_to_canonical[10] * point_xyz[2] + target_to_canonical[11];
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
	const float target_to_canonical[12], const float (*target_reference)[12],
	size_t target_joint_count, float *correction_out)
{
	double source_floor, target_floor, correction;
	const qboolean contacts = profile->contact_root[0] != NULL;
	if (!R_VRIKRenderMinimumBindZ (source, profile, true, contacts,
		target_to_canonical, NULL, 0, &source_floor) ||
		!R_VRIKRenderMinimumBindZ (target, profile, false, contacts,
			target_to_canonical, target_reference, target_joint_count, &target_floor))
		return false;
	correction = source_floor - target_floor;
	if (!isfinite (correction) || correction < -FLT_MAX || correction > FLT_MAX)
		return false;
	*correction_out = (float)correction;
	return true;
}

static qboolean R_VRIKRenderStageFloor (r_vrik_staged_avatar_t *selection,
	const qmodel_t *source, const qmodel_t *target)
{
	r_vrik_floor_cache_t *cache;
	const md5_skeleton_view_t *source_skeleton, *target_skeleton;
	float scale;
	if (!selection || !source || !target || !selection->target_rig.profile ||
		selection->id < 0 || selection->id >= R_VRIK_RENDER_MAX_AVATARS)
		return false;
	scale = R_AvatarQuantizedDisplayScale (selection->target_rig.profile);
	cache = &floor_cache[selection->id][selection->humanoid ? 1 : 0];
	source_skeleton = &selection->source_skeleton;
	target_skeleton = &selection->target_skeleton;
	if (!cache->attempted || cache->source_model != source ||
		cache->target_model != target ||
		cache->source_skeleton != source->md5_skeleton ||
		cache->target_skeleton != target->md5_skeleton ||
		cache->base_profile != selection->base_profile ||
		cache->display_scale != scale)
	{
		cache->attempted = true;
		cache->source_model = source;
		cache->target_model = target;
		cache->source_skeleton = source->md5_skeleton;
		cache->target_skeleton = target->md5_skeleton;
		cache->base_profile = selection->base_profile;
		cache->display_scale = scale;
		cache->valid = false;
		if (selection->humanoid)
		{
			float source_bind[R_VRIK_RENDER_MAX_JOINTS][12];
			float target_reference[R_VRIK_RENDER_MAX_JOINTS][12];
			if (source_skeleton->joint_count <= R_VRIK_RENDER_MAX_JOINTS &&
				target_skeleton->joint_count <= R_VRIK_RENDER_MAX_JOINTS)
			{
				for (size_t joint = 0; joint < source_skeleton->joint_count; ++joint)
					memcpy (source_bind[joint], source_skeleton->joints[joint].bind,
						sizeof (source_bind[joint]));
				if (R_AvatarRetargetHumanoid (&selection->source_rig,
					&selection->target_rig, &selection->presentation,
					&selection->humanoid_map, (const float *)source_bind,
					(float *)target_reference))
					cache->valid = R_VRIKRenderBindFloorCorrection (source, target,
						selection->target_rig.profile, selection->presentation.forward,
						(const float (*)[12])target_reference,
						target_skeleton->joint_count, &cache->correction_z);
			}
		}
		else
			cache->valid = R_VRIKRenderBindFloorCorrection (source, target,
				selection->target_rig.profile, selection->presentation.forward,
				NULL, 0, &cache->correction_z);
		if (!cache->valid)
			Con_Warning ("Avatar %s has no valid %s floor contact; %s\n",
				selection->base_profile->key,
				selection->humanoid ? "calibrated" : "bind",
				selection->humanoid ? "using generic retarget" : "using Ranger");
	}
	if (!cache->valid)
		return false;
	selection->floor_correction_z = cache->correction_z;
	R_AvatarPresentationAddCanonicalZ (&selection->presentation, cache->correction_z);
	memcpy (selection->target_to_canonical, selection->presentation.forward,
		sizeof (selection->target_to_canonical));
	return true;
}

static qboolean R_VRIKRenderResolveRigs (r_vrik_staged_avatar_t *selection,
	const qmodel_t *source, const qmodel_t *target, const r_avatar_profile_t *profile)
{
	r_vrik_rig_cache_t *cache = &rig_cache[selection->id];
	if (!cache->valid || cache->source_model != source || cache->target_model != target ||
		cache->source_skeleton != source->md5_skeleton ||
		cache->target_skeleton != target->md5_skeleton || cache->profile != profile)
	{
		memset (cache, 0, sizeof (*cache));
		if (!R_AvatarResolveRig (R_AvatarProfileForId (PLAYER_AVATAR_RANGER),
			&selection->source_skeleton, &cache->source_rig) ||
			!R_AvatarResolveRig (profile, &selection->target_skeleton, &cache->target_rig))
			return false;
		cache->source_model = source;
		cache->target_model = target;
		cache->source_skeleton = source->md5_skeleton;
		cache->target_skeleton = target->md5_skeleton;
		cache->profile = profile;
		/* Skeleton binds and semantic ownership are model lifetime data.
		 * Admission owns this work; frame preparation only transfers poses. */
		R_AvatarPrepareRetargetBinds (&cache->source_rig,
			&cache->target_rig, &cache->retarget_binds);
		/* Never retain a pointer to this caller's stack-owned skeleton view. */
		cache->source_rig.live = NULL;
		cache->target_rig.live = NULL;
		cache->valid = true;
	}
	selection->source_rig = cache->source_rig;
	selection->target_rig = cache->target_rig;
	selection->source_rig.live = &selection->source_skeleton;
	selection->target_rig.live = &selection->target_skeleton;
	selection->retarget_binds = cache->retarget_binds.valid ?
		&cache->retarget_binds : NULL;
	return true;
}

static qboolean R_VRIKRenderStageGenericPresentation (r_vrik_staged_avatar_t *selection)
{
	r_vrik_rig_cache_t *cache = &rig_cache[selection->id];
	selection->target_rig.profile = selection->base_profile;
	if (!cache->generic_attempted)
	{
		cache->generic_attempted = true;
		cache->generic_valid = R_AvatarBuildPresentationContext (
			&selection->source_rig, &selection->target_rig,
			&cache->generic_presentation);
	}
	if (!cache->generic_valid)
		return false;
	selection->presentation = cache->generic_presentation;
	return true;
}

static qboolean R_VRIKRenderStageHumanoidPresentation (r_vrik_staged_avatar_t *selection)
{
	r_vrik_rig_cache_t *cache = &rig_cache[selection->id];
	if (!cache->humanoid_attempted)
	{
		const float source_height = R_AvatarHumanoidHeight (&selection->source_rig);
		const float target_height = R_AvatarHumanoidHeight (&selection->target_rig);
		cache->humanoid_attempted = true;
		if (source_height > 0.001f && target_height > 0.001f)
		{
			r_avatar_profile_t normalized = *selection->base_profile;
			normalized.display_scale *= source_height / target_height;
			if (isfinite (normalized.display_scale) && normalized.display_scale > 0.0f)
			{
				r_avatar_rig_t target_rig = selection->target_rig;
				target_rig.profile = &normalized;
				cache->humanoid_valid = R_AvatarBuildPresentationContext (
					&selection->source_rig, &target_rig,
					&cache->humanoid_presentation) &&
					R_AvatarBuildHumanoid (&selection->source_rig, &target_rig,
						&cache->humanoid_presentation, &cache->humanoid_map);
				if (cache->humanoid_valid)
					cache->humanoid_display_scale = normalized.display_scale;
			}
		}
	}
	if (!cache->humanoid_valid)
		return false;
	selection->normalized_profile = *selection->base_profile;
	selection->normalized_profile.display_scale = cache->humanoid_display_scale;
	selection->target_rig.profile = &selection->normalized_profile;
	selection->presentation = cache->humanoid_presentation;
	selection->humanoid_map = cache->humanoid_map;
	selection->humanoid = true;
	return true;
}

static int R_VRIKRenderQBJ3CorpseOwner (const entity_t *entity)
{
	uintptr_t address;
	int slot;

	if (!R_VRIKRenderQBJ3Game () || !cl.entities || !cl.scores || !entity ||
		cl.maxclients < 1 || cl.maxclients >= cl.num_entities ||
		cl.num_entities <= cl.maxclients + 1 ||
		!R_VRIKRenderQBJ3Death (entity) || entity->colormap == vid.colormap)
		return -1;
	address = (uintptr_t)entity;
	/* Only dynamic entities beyond the reserved player slots can be queued
	 * corpses. Static entities and arbitrary aliases have no owner identity. */
	if (address < (uintptr_t)&cl.entities[cl.maxclients + 1] ||
		address >= (uintptr_t)&cl.entities[cl.num_entities])
		return -1;
	for (slot = 0; slot < cl.maxclients && slot < MAX_SCOREBOARD; ++slot)
		if (cl.scores[slot].name[0] &&
			entity->colormap == cl.scores[slot].translations)
			return slot;
	return -1;
}

static qboolean R_VRIKRenderExplicitAvatarForSlot (int owner_slot, int *id_out)
{
	int id;
	if (!id_out || owner_slot < 0 || owner_slot >= MAX_SCOREBOARD)
		return false;
	id = cl.avatar_ids[owner_slot];
	if (id <= PLAYER_AVATAR_RANGER || id >= R_VRIK_RENDER_MAX_AVATARS ||
		!R_VRIKRenderProfileForId (id))
		return false;
	*id_out = id;
	return true;
}

static void R_VRIKRenderRebindSelection (r_vrik_staged_avatar_t *selection)
{
	if (!selection || !selection->valid)
		return;
	selection->source_rig.live = &selection->source_skeleton;
	selection->target_rig.live = &selection->target_skeleton;
	if (selection->humanoid)
		selection->target_rig.profile = &selection->normalized_profile;
}

static qboolean R_VRIKRenderStageSelection (r_vrik_frame_entry_t *entry, int id)
{
	r_vrik_staged_avatar_t selection = {0};
	const entity_t *entity;
	qmodel_t *source, *target;
	const r_avatar_profile_t *profile;
	const custom_avatar_t *custom = NULL;
	vrik_pose_t pose;
	int selected_id = id;
	qboolean qbj3_game, implicit_qbj3 = false;

	if (!entry)
		return false;
	entity = entry->entity;
	if (!entity)
		return false;
	qbj3_game = R_VRIKRenderQBJ3Game ();
	if (!R_VRIKRenderOriginalModelEligible (entity))
		return false;
	if (qbj3_game && id == PLAYER_AVATAR_RANGER)
	{
		/* The receiver leaves unresolved custom descriptors at Ranger. Keep
		 * QBJ3's native art for those descriptors instead of inferring a pack. */
		if (!R_VRIKRenderQBJ3LivePlayer (entity) ||
			cl.avatar_custom_keys[entry->owner_slot][0] || !VR_InputVRIKAllowed () ||
			!R_VRIKSampleEntityPose (entity, &pose))
			return false;
		selected_id = CustomAvatar_IdForKey ("qbj3");
		if (selected_id < PLAYER_AVATAR_COUNT || selected_id >= R_VRIK_RENDER_MAX_AVATARS)
			return false;
		custom = CustomAvatar_Get (selected_id);
		if (!custom || CustomAvatar_HasFailed (selected_id) ||
			custom->profile.equipment_policy != R_AVATAR_EQUIPMENT_RANGER)
			return false;
		implicit_qbj3 = true;
	}
	else if (id == PLAYER_AVATAR_RANGER)
		return false;
	if (selected_id < 0 || selected_id >= R_VRIK_RENDER_MAX_AVATARS)
		return false;
	profile = R_VRIKRenderProfileForId (selected_id);
	if (!profile)
		return false;

	source = R_VRIKRenderBuiltinModel (PLAYER_AVATAR_RANGER);
	target = source ? (selected_id < PLAYER_AVATAR_COUNT ? R_VRIKRenderBuiltinModel (selected_id) :
		R_VRIKRenderCustomModel (selected_id)) : NULL;
	selection.id = selected_id;
	selection.implicit_qbj3 = implicit_qbj3;
	if (!source || !target || !Mod_GetMD5Skeleton (source, &selection.source_skeleton) ||
		!selection.source_skeleton.from_rerelease ||
		!Mod_GetMD5Skeleton (target, &selection.target_skeleton) ||
		!R_VRIKRenderResolveRigs (&selection, source, target, profile))
		return false;
	selection.base_profile = profile;
	if (r_avatar_humanoid.value != 0.0f && CustomAvatar_Get (selected_id))
		R_VRIKRenderStageHumanoidPresentation (&selection);
	selection.humanoid_ik = selection.humanoid && r_avatar_humanoid.value != 2.0f;
	if ((!selection.humanoid && !R_VRIKRenderStageGenericPresentation (&selection)) ||
		!R_VRIKRenderStageFloor (&selection, source, target))
	{
		if (!selection.humanoid)
			return false;
		/* Missing optional reference influences leave the original custom
		 * package available through its generic retargeting policy. */
		selection.humanoid = false;
		selection.humanoid_ik = false;
		if (!R_VRIKRenderStageGenericPresentation (&selection) ||
			!R_VRIKRenderStageFloor (&selection, source, target))
			return false;
	}
	if (!isfinite (selection.target_to_canonical[11]))
		return false;
	selection.source_geometry = (const aliashdr_t *)source->extradata[PV_MD5];
	selection.target_geometry = (const aliashdr_t *)target->extradata[PV_MD5];
	if (!selection.source_geometry || !selection.target_geometry ||
		(qbj3_game && selection.source_geometry->numframes < 143) ||
		selection.source_geometry->numjoints != (int)selection.source_skeleton.joint_count ||
		selection.target_geometry->numjoints != (int)selection.target_skeleton.joint_count ||
		(selection.source_geometry->poseverttype != PV_MD5 && selection.source_geometry->poseverttype != PV_MD5_8) ||
		(selection.target_geometry->poseverttype != PV_MD5 && selection.target_geometry->poseverttype != PV_MD5_8))
		return false;
	int surface_count = 0;
	for (const aliashdr_t *surface = selection.target_geometry; surface; surface = surface->nextsurface)
		if (++surface_count > MAX_SURFACES ||
			(surface->poseverttype != PV_MD5 && surface->poseverttype != PV_MD5_8) ||
			surface->numjoints != (int)selection.target_skeleton.joint_count)
			return false;
	selection.entity = entity;
	selection.original_model = entity->model;
	selection.source_model = source;
	selection.target_model = target;
	selection.source_skeleton_data = source->md5_skeleton;
	selection.target_skeleton_data = target->md5_skeleton;
	selection.valid = true;
	entry->selection = selection;
	/* ResolveRigs borrowed the local selection. Rebind after copying into the
	 * reserved frame entry; the vector will not grow while it is being filled. */
	R_VRIKRenderRebindSelection (&entry->selection);
	entry->selection_valid = true;
	entry->qbj3_death = qbj3_game && R_VRIKRenderQBJ3Death (entity);
	return true;
}

void R_VRIKRenderStageAvatars (void)
{
	size_t player_count, corpse_count = 0, required, count = 0;
	int maxclients;

	R_VRIKRenderInvalidatePublication ();
	if (strcmp (admission_gamedir, com_gamedir))
		R_VRIKRenderResetModelCaches ();
	if (!cl.entities || !cl.worldmodel || con_forcedup || cl.num_entities <= 1)
		return;
	maxclients = q_min (MAX_SCOREBOARD,
		q_min (q_max (0, cl.maxclients), cl.num_entities - 1));
	player_count = (size_t)maxclients;
	if (R_VRIKRenderQBJ3Game () && cl.maxclients > 0 &&
		cl.maxclients < cl.num_entities)
	{
		for (int entitynum = cl.maxclients + 1; entitynum < cl.num_entities; ++entitynum)
		{
			const int owner_slot = R_VRIKRenderQBJ3CorpseOwner (&cl.entities[entitynum]);
			int id;
			if (owner_slot >= 0 && R_VRIKRenderExplicitAvatarForSlot (owner_slot, &id))
				++corpse_count;
		}
	}
	if (corpse_count > SIZE_MAX - player_count)
		required = SIZE_MAX;
	else
		required = player_count + corpse_count;
	/* A failed realloc leaves the previous capacity intact; use that capacity
	 * for players first, then as many corpses as fit. */
	(void)R_VRIKRenderReserveEntries (required);

	/* Reserve first, then fill all player slots before queued corpses. Model
	 * cache resets during first-load admission never clear this frame list. */
	for (int player = 1; player <= maxclients && count < frame_entries.capacity; ++player)
	{
		r_vrik_frame_entry_t *entry = &frame_entries.entries[count++];
		memset (entry, 0, sizeof (*entry));
		entry->entity = &cl.entities[player];
		entry->owner_slot = player - 1;
		entry->selected_id = cl.avatar_ids[player - 1];
		entry->custom_key_empty = !cl.avatar_custom_keys[player - 1][0];
		entry->qbj3_death = R_VRIKRenderQBJ3Game () &&
			R_VRIKRenderQBJ3Death (entry->entity);
		R_VRIKRenderStageSelection (entry, entry->selected_id);
	}
	if (R_VRIKRenderQBJ3Game () && cl.maxclients > 0 &&
		cl.maxclients < cl.num_entities)
	{
		for (int entitynum = cl.maxclients + 1;
			entitynum < cl.num_entities && count < frame_entries.capacity; ++entitynum)
		{
			const int owner_slot = R_VRIKRenderQBJ3CorpseOwner (&cl.entities[entitynum]);
			int id;
			if (owner_slot < 0 || !R_VRIKRenderExplicitAvatarForSlot (owner_slot, &id))
				continue;
			r_vrik_frame_entry_t *entry = &frame_entries.entries[count++];
			memset (entry, 0, sizeof (*entry));
			entry->entity = &cl.entities[entitynum];
			entry->owner_slot = owner_slot;
			entry->selected_id = id;
			entry->corpse = true;
			entry->qbj3_death = true;
			R_VRIKRenderStageSelection (entry, id);
		}
	}
	frame_entries.count = count;
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

/* The source owns both private meshes. A partial upload, unusable material or
 * stale view rejects the entire alternate, including its body selection. */
static qboolean R_VRIKRenderValidatePropView (const qmodel_t *source, int prop,
	int skinnum, double *qmax_out)
{
	const md5_avatar_prop_surface_t *cpu;
	const aliashdr_t *gpu;
	double qmax = 0.0;
	int surface_count = 0;
	int total_verts = 0, total_indexes = 0;

	if (!source || prop < 0 || prop >= MD5_AVATAR_PROP_COUNT || !qmax_out ||
		!source->avatar_props[prop].surfaces || !source->avatar_prop_gpu[prop] ||
		source->avatar_props[prop].numverts <= 0 ||
		source->avatar_props[prop].numindexes <= 0)
		return false;
	for (cpu = source->avatar_props[prop].surfaces,
		gpu = source->avatar_prop_gpu[prop]; cpu && gpu;
		cpu = cpu->next, gpu = gpu->nextsurface)
	{
		if (++surface_count > MAX_SURFACES || !cpu->vertices || !cpu->indexes ||
			cpu->numverts <= 0 || cpu->numindexes <= 0 ||
			gpu->numverts_vbo != cpu->numverts ||
			gpu->numindexes != cpu->numindexes ||
			gpu->numtris != cpu->numindexes / 3 ||
			gpu->poseverttype != PV_MD5 || gpu->numjoints != 1 ||
			gpu->numframes != 1 || gpu->numposes != 1 ||
			!gpu->avatar_static_prop || gpu->numskins < 1 ||
			gpu->numskins > MAX_SKINS ||
			gpu->vertex_buffer == VK_NULL_HANDLE ||
			gpu->index_buffer == VK_NULL_HANDLE ||
			gpu->joints_buffer == VK_NULL_HANDLE ||
			gpu->joints_set == VK_NULL_HANDLE ||
			!gpu->tracked_cull_qmax_valid ||
			!isfinite(gpu->tracked_cull_qmax) || gpu->tracked_cull_qmax < 0.0)
			return false;
		for (int axis = 0; axis < 3; ++axis)
			if (gpu->scale[axis] != 1.0f || gpu->scale_origin[axis] != 0.0f)
				return false;
		const int skin = skinnum >= 0 && skinnum < gpu->numskins ? skinnum : 0;
		const int anim = (int)(cl.time * 10) & 3;
		const gltexture_t *tx = gpu->gltextures[skin][anim];
		const gltexture_t *fb = gpu->fbtextures[skin][anim];
		if (!tx || tx->descriptor_set == VK_NULL_HANDLE ||
			(fb && fb->descriptor_set == VK_NULL_HANDLE))
			return false;
		if (qmax < gpu->tracked_cull_qmax)
			qmax = gpu->tracked_cull_qmax;
		if (total_verts > INT_MAX - cpu->numverts ||
			total_indexes > INT_MAX - cpu->numindexes)
			return false;
		total_verts += cpu->numverts;
		total_indexes += cpu->numindexes;
	}
	if (cpu || gpu || !surface_count ||
		total_verts != source->avatar_props[prop].numverts ||
		total_indexes != source->avatar_props[prop].numindexes)
		return false;
	*qmax_out = qmax;
	return true;
}

static void R_VRIKRenderMuzzleCandidate (const entity_t *entity, const aliashdr_t *geometry,
	const vec3_t point, r_vrik_candidate_t *candidate)
{
	lerpdata_t lerpdata = {0};
	float matrix[16];
	const double pose_age = realtime - entity->vrik_pose_times[0];
	if (entity->vrik_slot_retired || !isfinite (pose_age) ||
		pose_age < 0.0 || pose_age > VRIK_POSE_STALE_TIME)
		return;
	R_GetEntityLerpedTransform (entity, lerpdata.origin, lerpdata.angles);
	if (candidate->tracked_root_valid && isfinite (candidate->tracked_root_yaw))
		lerpdata.angles[YAW] = candidate->tracked_root_yaw;
	/* R_AliasModelMatrix only reads the entity; its existing API is non-const. */
	if (R_AliasModelMatrix ((entity_t *)entity, geometry, &lerpdata, matrix) < 0)
		return;
	for (int axis = 0; axis < 3; ++axis)
	{
		candidate->muzzle.origin[axis] = matrix[axis] * point[0] +
			matrix[4 + axis] * point[1] + matrix[8 + axis] * point[2] + matrix[12 + axis];
		if (!isfinite (candidate->muzzle.origin[axis]))
			return;
	}
	candidate->muzzle.valid = true;
}

static qboolean R_VRIKRenderAttachProp (
	const r_vrik_staged_avatar_t *selection,
	const r_avatar_rig_t *source_rig, const r_avatar_rig_t *target_rig,
	const vrik_pose_t *pose, qboolean tracked, qboolean muzzle_valid,
	const float (*source_palette)[12], const float (*target_palette)[12],
	r_vrik_candidate_t *candidate)
{
	const r_avatar_profile_t *profile = target_rig->profile;
	const r_avatar_presentation_context_t *context = &selection->presentation;
	int hand_semantic, source_hand, target_hand;
	int selected_prop = -1, selected_joint = -1;
	double closest = DBL_MAX, qmax;
	double rotation_squared = 0.0, translation_squared = 0.0;

	candidate->attached_prop_geometry = NULL;
	candidate->attached_prop_valid = false;
	candidate->attached_prop_local_bound = 0.0;
	memset(candidate->attached_prop_to_canonical, 0,
		sizeof(candidate->attached_prop_to_canonical));
	/* Native custom packages keep their skinned props. The default Ranger
	 * policy attaches this prop; package authors must omit embedded gear,
	 * since the custom manifest has no native-equipment joint names to filter. */
	if (profile->equipment_policy != R_AVATAR_EQUIPMENT_ATTACH_HAND)
		return profile->equipment_policy == R_AVATAR_EQUIPMENT_RANGER;
	if (!pose)
		return false;
	hand_semantic = tracked && (pose->flags & VRIK_FLAG_DOMINANT_LEFT) ?
		MD5_VRIK_HAND_L : MD5_VRIK_HAND_R;
	source_hand = source_rig->joint[hand_semantic];
	target_hand = target_rig->joint[hand_semantic];
	if (source_hand < 0 || target_hand < 0 ||
		!selection->source_model->avatar_builtin ||
		!isfinite(selection->floor_correction_z))
		return false;
	for (int prop = 0; prop < MD5_AVATAR_PROP_COUNT; ++prop)
	{
		const int semantic = prop == MD5_AVATAR_PROP_GUN ? MD5_VRIK_GUN : MD5_VRIK_AXE;
		const int joint = source_rig->joint[semantic];
		double squared = 0.0;
		if (joint < 0)
			continue;
		for (int axis = 0; axis < 3; ++axis)
		{
			const double delta = (double)source_palette[joint][axis * 4 + 3] -
				source_palette[source_hand][axis * 4 + 3];
			squared += delta * delta;
		}
		if (!isfinite(squared))
			return false;
		if (squared < closest) /* Source chooses Gun on an exact tie. */
		{
			closest = squared;
			selected_prop = prop;
			selected_joint = joint;
		}
	}
	if (selected_prop < 0 || closest > 64.0 * 64.0 ||
		!R_VRIKRenderValidatePropView(selection->source_model,
			selected_prop, selection->entity->skinnum, &qmax) ||
		/* The AS task builds the shared prop BLAS after palette preparation.
		 * Until a prior submitted frame has built it, keep the complete Ranger
		 * presentation rather than publishing a body without its ray caster. */
		(vulkan_globals.ray_query && bmodel_tlas != VK_NULL_HANDLE &&
		 r_gpulightmapupdate.value &&
		 r_rtshadows.value > 0 &&
		 !GLMesh_AvatarPropBLASReady (
			selection->source_model->avatar_prop_gpu[selected_prop])) ||
		!R_AvatarBuildAttachedPropTransform(context,
			source_palette[source_hand],
			source_rig->live->joints[source_hand].bind,
			target_palette[target_hand],
			selection->humanoid ? selection->humanoid_map.reference[hand_semantic] :
				target_rig->live->joints[target_hand].bind,
			source_palette[selected_joint],
			candidate->attached_prop_to_canonical))
		return false;
	for (int row = 0; row < 3; ++row)
	{
		for (int column = 0; column < 3; ++column)
		{
			const double value = candidate->attached_prop_to_canonical[row * 4 + column];
			rotation_squared += value * value;
		}
		const double value = candidate->attached_prop_to_canonical[row * 4 + 3];
		translation_squared += value * value;
	}
	candidate->attached_prop_local_bound =
		sqrt(translation_squared) + sqrt(rotation_squared) * qmax;
	if (!isfinite(candidate->attached_prop_local_bound) ||
		candidate->attached_prop_local_bound < 0.0)
		return false;
	candidate->attached_prop_geometry =
		selection->source_model->avatar_prop_gpu[selected_prop];
	candidate->attached_prop_valid = true;
	if (tracked && muzzle_valid && selected_prop == MD5_AVATAR_PROP_GUN)
	{
		vec3_t tip;
		/* Measured bone-local +Y tip, through the actual prop attachment.
		 * Target-body display scale/floor affine does not apply to this prop. */
		for (int axis = 0; axis < 3; ++axis)
			tip[axis] = candidate->attached_prop_to_canonical[axis * 4 + 1] * 20.0f +
				candidate->attached_prop_to_canonical[axis * 4 + 3];
		R_VRIKRenderMuzzleCandidate (selection->entity, candidate->attached_prop_geometry, tip, candidate);
	}
	return true;
}

static qboolean R_VRIKRenderCandidate (const entity_t *entity, r_vrik_candidate_t *candidate, float (*palette)[12])
{
	aliashdr_t *header;
	r_vrik_palette_output_t output;
	r_vrik_lowerbody_targets_t lower_targets;
	const r_vrik_lowerbody_targets_t *lower_input = NULL;
	qboolean qbj3_game;

	if (!entity || !candidate || !palette)
		return false;
	memset (&candidate->muzzle, 0, sizeof (candidate->muzzle));
	candidate->tracked_root_yaw = 0.0f;
	candidate->tracked_root_valid = false;
	qbj3_game = R_VRIKRenderQBJ3Game ();
	if (qbj3_game && !R_VRIKRenderQBJ3LivePlayer (entity))
		return false;
	if (!VR_InputVRIKAllowed () || !R_VRIKSampleEntityPose (entity, &candidate->pose))
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
	if (R_VRIKBuildRangerPalette (&candidate->skeleton, &candidate->lerpdata,
		&candidate->pose, lower_input, candidate->muzzleflash, &output) != R_VRIK_PALETTE_OK ||
		output.joint_count > UINT32_MAX)
		return false;
	if (isfinite (candidate->pose.body_yaw))
	{
		candidate->tracked_root_yaw = candidate->pose.body_yaw;
		candidate->tracked_root_valid = true;
	}
	if (output.muzzle_valid)
		R_VRIKRenderMuzzleCandidate (entity, header, output.muzzle_origin, candidate);

	candidate->entity = entity;
	candidate->model = entity->model;
	candidate->geometry = header;
	candidate->joint_count = (uint32_t)output.joint_count;
	candidate->alternate_avatar = false;
	candidate->attached_prop_geometry = NULL;
	candidate->attached_prop_valid = false;
	candidate->attached_prop_local_bound = 0.0;
	memset(candidate->attached_prop_to_canonical, 0,
		sizeof(candidate->attached_prop_to_canonical));
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

static qboolean R_VRIKRenderRawHumanoidGoal (
	const r_vrik_staged_avatar_t *selection,
	const r_vrik_palette_output_t *ranger, const float raw[3], float goal[3])
{
	vec3_t model;
	if (!selection || !ranger || !ranger->body_basis_valid || !raw || !goal)
		return false;
	for (int axis = 0; axis < 3; ++axis)
		model[axis] = ranger->body_basis[1][axis] * raw[0] -
			ranger->body_basis[0][axis] * raw[1] +
			ranger->body_basis[2][axis] * raw[2];
	R_AvatarPresentationInversePoint (&selection->presentation, model, goal);
	return isfinite(goal[0]) && isfinite(goal[1]) && isfinite(goal[2]);
}

/* The canonical Ranger solve may clamp tracked reach. Use its original
 * pre-solve basis to map raw accepted goals into the selected body's frame,
 * then solve on that body's physical limb lengths. */
static qboolean R_VRIKRenderRefineHumanoidTracked (
	const r_vrik_staged_avatar_t *selection, const vrik_pose_t *pose,
	const r_vrik_lowerbody_targets_t *lower,
	const r_vrik_palette_output_t *ranger, float (*palette)[12])
{
	const r_avatar_rig_t *rig = &selection->target_rig;
	const md5_skeleton_view_t *skeleton = &selection->target_skeleton;
	float saved[R_VRIK_RENDER_MAX_JOINTS][12], goals[19][3], endpoint[12], pole[3];
	unsigned int targets = 0;
	static const int tracker_semantics[VRIK_TRACKER_COUNT] = {
		MD5_VRIK_HEAD, MD5_VRIK_HAND_L, MD5_VRIK_HAND_R};
	static const int lower_semantics[R_VRIK_LOWER_ROLE_COUNT] = {
		MD5_VRIK_HIP, MD5_VRIK_FOOT_L, MD5_VRIK_FOOT_R};
	static const int upper_semantics[4] = {
		MD5_VRIK_UPPERARM_L, MD5_VRIK_UPPERARM_R,
		MD5_VRIK_UPPERLEG_L, MD5_VRIK_UPPERLEG_R};
	if (!pose || !ranger || !ranger->body_basis_valid || !palette ||
		!rig->valid || !skeleton->joint_count ||
		skeleton->joint_count > R_VRIK_RENDER_MAX_JOINTS)
		return false;
	memcpy (saved, palette, skeleton->joint_count * sizeof (palette[0]));
	/* Animation feet are the fallback goals for head-only crouches. */
	for (int role = R_VRIK_LOWER_LEFT_FOOT; role <= R_VRIK_LOWER_RIGHT_FOOT; ++role)
	{
		const int semantic = lower_semantics[role], joint = rig->joint[semantic];
		if (joint < 0 || (size_t)joint >= skeleton->joint_count)
			return false;
		for (int axis = 0; axis < 3; ++axis)
			goals[semantic][axis] = palette[joint][axis * 4 + 3];
		targets |= 1u << semantic;
	}
	for (int tracker = 0; tracker < VRIK_TRACKER_COUNT; ++tracker)
	{
		const unsigned char flag = tracker == VRIK_TRACKER_HEAD ?
			VRIK_FLAG_HEAD_TRACKED : tracker == VRIK_TRACKER_LEFT_HAND ?
			VRIK_FLAG_LEFT_HAND_TRACKED : VRIK_FLAG_RIGHT_HAND_TRACKED;
		if (!(pose->flags & flag))
			continue;
		const int semantic = tracker_semantics[tracker];
		if (!R_VRIKRenderRawHumanoidGoal (selection, ranger,
			pose->position[tracker], goals[semantic]))
			return false;
		targets |= 1u << semantic;
	}
	if (lower)
		for (int role = 0; role < R_VRIK_LOWER_ROLE_COUNT; ++role)
		{
			const unsigned char bit = R_VRIK_LOWER_BIT (role);
			if (!(lower->present_mask & bit) ||
				!((lower->tracked_mask | lower->predicted_mask) & bit) ||
				!isfinite (lower->confidence[role]) || lower->confidence[role] <= 0.0f)
				continue;
			const int semantic = lower_semantics[role];
			if (!R_VRIKRenderRawHumanoidGoal (selection, ranger,
				lower->position[role], goals[semantic]))
				return false;
			targets |= 1u << semantic;
		}
	const int anchor = (targets & (1u << MD5_VRIK_HIP)) ?
		MD5_VRIK_HIP : MD5_VRIK_HEAD;
	if (targets & (1u << anchor))
	{
		const int root = rig->joint[MD5_VRIK_HIP], anchor_joint = rig->joint[anchor];
		float delta[3];
		if (root < 0 || anchor_joint < 0 ||
			(size_t)root >= skeleton->joint_count ||
			(size_t)anchor_joint >= skeleton->joint_count)
			return false;
		for (int axis = 0; axis < 3; ++axis)
			delta[axis] = goals[anchor][axis] - palette[anchor_joint][axis * 4 + 3];
		for (size_t joint = 0; joint < skeleton->joint_count; ++joint)
		{
			int parent = (int)joint;
			while (parent >= 0 && parent != root)
				parent = skeleton->joints[parent].parent;
			if (parent != root)
				continue;
			for (int axis = 0; axis < 3; ++axis)
			{
				palette[joint][axis * 4 + 3] += delta[axis];
				if (!isfinite (palette[joint][axis * 4 + 3]))
				{
					memcpy (palette, saved, skeleton->joint_count * sizeof (palette[0]));
					return false;
				}
			}
		}
	}
	for (int limb = 0; limb < 4; ++limb)
	{
		const int upper = upper_semantics[limb], end = upper + 2;
		const int lower_joint = rig->joint[upper + 1], end_joint = rig->joint[end];
		if (!(targets & (1u << end)))
			continue;
		if (lower_joint < 0 || end_joint < 0 ||
			(size_t)lower_joint >= skeleton->joint_count ||
			(size_t)end_joint >= skeleton->joint_count)
			continue;
		memcpy (endpoint, palette[end_joint], sizeof (endpoint));
		for (int axis = 0; axis < 3; ++axis)
		{
			endpoint[axis * 4 + 3] = goals[end][axis];
			pole[axis] = palette[lower_joint][axis * 4 + 3];
		}
		/* Each solve restores its input on failure. Other tracked limbs remain
		 * useful, so keep their independently successful solves. */
		R_AvatarSolveHumanoidLimb (rig, (float *)palette, upper, endpoint, pole);
	}
	return true;
}

static qboolean R_VRIKRenderAlternateCandidate (const entity_t *entity,
	const r_vrik_staged_avatar_t *selection, r_vrik_candidate_t *candidate, float (*palette)[12])
{
	const md5_skeleton_view_t *source_skeleton, *target_skeleton;
	const r_avatar_rig_t *source_rig, *target_rig;
	r_vrik_palette_output_t ranger;
	r_vrik_lowerbody_targets_t lower_targets;
	const r_vrik_lowerbody_targets_t *lower_input = NULL;
	vrik_pose_t pose = {0};
	entity_t canonical_entity;
	float source_palette[R_VRIK_RENDER_MAX_JOINTS][12];
	r_vrik_palette_result_t result;
	qboolean tracked, qbj3_death;

	memset (&candidate->muzzle, 0, sizeof (candidate->muzzle));
	candidate->tracked_root_yaw = 0.0f;
	candidate->tracked_root_valid = false;
	if (!selection)
		return false;
	source_skeleton = &selection->source_skeleton;
	target_skeleton = &selection->target_skeleton;
	source_rig = &selection->source_rig;
	target_rig = &selection->target_rig;
	if (!selection->valid || selection->entity != entity ||
		selection->id <= PLAYER_AVATAR_RANGER || !R_VRIKRenderProfileForId (selection->id) ||
		!R_VRIKRenderOriginalModelEligible (entity) || entity->model != selection->original_model ||
		!selection->source_model || !selection->target_model ||
		selection->source_model->needload || selection->target_model->needload ||
		!selection->source_model->avatar_builtin ||
		!Mod_IsAdmittedAvatarModel (selection->target_model) ||
		(selection->id < PLAYER_AVATAR_COUNT ? !selection->target_model->avatar_builtin :
		 selection->target_model->avatar_custom_id != selection->id) ||
		selection->source_model->extradata[PV_MD5] != (const byte *)selection->source_geometry ||
		selection->target_model->extradata[PV_MD5] != (const byte *)selection->target_geometry ||
		selection->source_model->md5_skeleton != selection->source_skeleton_data ||
		selection->target_model->md5_skeleton != selection->target_skeleton_data ||
		!source_skeleton->from_rerelease ||
		source_skeleton->joint_count != (size_t)selection->source_geometry->numjoints ||
		target_skeleton->joint_count != (size_t)selection->target_geometry->numjoints ||
		target_skeleton->joint_count > R_VRIK_RENDER_MAX_JOINTS ||
		!source_rig->valid || !target_rig->valid ||
		source_rig->live != source_skeleton || target_rig->live != target_skeleton ||
		source_rig->profile != R_AvatarProfileForId (PLAYER_AVATAR_RANGER) ||
		selection->base_profile != R_VRIKRenderProfileForId (selection->id) ||
		target_rig->profile != (selection->humanoid ?
			&selection->normalized_profile : selection->base_profile))
		return false;
	qbj3_death = R_VRIKRenderQBJ3Game () && R_VRIKRenderQBJ3Death (entity);

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
	tracked = !qbj3_death && VR_InputVRIKAllowed () &&
		R_VRIKSampleEntityPose (entity, &pose);
	if (selection->implicit_qbj3 && !tracked)
		return false;
	if (tracked)
	{
		if (R_VRIKSampleEntityLowerTargets (entity, &lower_targets))
			lower_input = &lower_targets;
		result = R_VRIKBuildRangerPalette (source_skeleton, &candidate->lerpdata, &pose,
			lower_input, (entity->effects & EF_MUZZLEFLASH) != 0, &ranger);
	}
	else
		result = R_VRIKBuildRangerAnimationPalette (source_skeleton, &candidate->lerpdata, &ranger);
	if (result != R_VRIK_PALETTE_OK ||
		ranger.joint_count != source_skeleton->joint_count)
		return false;
	if (tracked && isfinite (pose.body_yaw))
	{
		candidate->tracked_root_yaw = pose.body_yaw;
		candidate->tracked_root_valid = true;
	}
	const qboolean profile_retarget = scr_speeds.value == 3;
	const double retarget_start = profile_retarget ? Sys_DoubleTime () : 0.0;
	const qboolean retarget_failed = selection->humanoid ?
			!R_AvatarRetargetHumanoid (source_rig, target_rig,
				&selection->presentation, &selection->humanoid_map,
				(const float *)source_palette, (float *)palette) :
			/* Staging already resolved this bind-only transform. Rebuilding it
			 * for every generic pose repeats the same rig basis work. */
			!(selection->retarget_binds ?
				R_AvatarRetargetPalettePreparedWithContext (source_rig, target_rig,
					&selection->presentation, selection->retarget_binds,
					(const float *)source_palette, (float *)palette) :
				R_AvatarRetargetPaletteWithContext (source_rig, target_rig,
					&selection->presentation, (const float *)source_palette,
					(float *)palette));
	if (profile_retarget)
	{
		rs_avatarretarget_us += (Sys_DoubleTime () - retarget_start) * 1000000.0;
		++rs_avatarretarget_count;
	}
	if (retarget_failed)
		return false;
	/* Optional tracked repairs retain successful independent stages. The
	 * canonical palette and supplied lower targets remain authoritative. */
	unsigned char tracked_lower_mask = 0;
	float tracked_lower_confidence[3] = {1.0f, 1.0f, 1.0f};
	if (lower_input)
	{
		const unsigned char usable = lower_input->present_mask &
			(lower_input->tracked_mask | lower_input->predicted_mask);
		if (usable & R_VRIK_LOWER_BIT (R_VRIK_LOWER_LEFT_FOOT))
		{
			tracked_lower_mask |= R_AVATAR_TRACKED_FOOT_L;
			tracked_lower_confidence[0] = lower_input->confidence[R_VRIK_LOWER_LEFT_FOOT];
		}
		if (usable & R_VRIK_LOWER_BIT (R_VRIK_LOWER_RIGHT_FOOT))
		{
			tracked_lower_mask |= R_AVATAR_TRACKED_FOOT_R;
			tracked_lower_confidence[1] = lower_input->confidence[R_VRIK_LOWER_RIGHT_FOOT];
		}
		if (usable & R_VRIK_LOWER_BIT (R_VRIK_LOWER_HIP))
		{
			tracked_lower_mask |= R_AVATAR_TRACKED_HIP;
			tracked_lower_confidence[2] = lower_input->confidence[R_VRIK_LOWER_HIP];
		}
	}
	if (!selection->humanoid)
		R_AvatarRefineBuiltinPaletteForFrame (source_rig, target_rig, tracked,
			(const float (*)[12])source_palette, selection->floor_correction_z,
			tracked_lower_mask, palette, R_VRIK_RENDER_MAX_JOINTS,
			&selection->presentation, tracked_lower_confidence);
	else if (selection->humanoid_ik && tracked &&
		!R_VRIKRenderRefineHumanoidTracked (selection, &pose, lower_input,
			&ranger, palette))
		return false;
	else if (selection->humanoid_ik && !tracked)
	{
		float endpoint[12], pole[3];
		const int elbow = target_rig->joint[MD5_VRIK_LOWERARM_L];
		if (elbow >= 0 && R_AvatarHumanoidDesktopSupportEndpoint (
			source_rig, target_rig, &selection->presentation,
			&selection->humanoid_map, (const float *)source_palette,
			(const float *)palette, endpoint))
		{
			for (int axis = 0; axis < 3; ++axis)
				pole[axis] = palette[elbow][axis * 4 + 3];
			R_AvatarSolveHumanoidLimb (target_rig, (float *)palette,
				MD5_VRIK_UPPERARM_L, endpoint, pole);
		}
	}
	if (!R_VRIKRenderAttachProp(selection, source_rig, target_rig,
		&pose, tracked, tracked && ranger.muzzle_valid, (const float (*)[12])source_palette,
		(const float (*)[12])palette, candidate))
	{
		if (!qbj3_death)
			return false;
		/* Optional gear must not suppress a complete death body. */
		candidate->attached_prop_geometry = NULL;
		candidate->attached_prop_valid = false;
		candidate->attached_prop_local_bound = 0.0;
		memset (candidate->attached_prop_to_canonical, 0,
			sizeof (candidate->attached_prop_to_canonical));
	}
	if (qbj3_death)
	{
		candidate->tracked_root_yaw = 0.0f;
		candidate->tracked_root_valid = false;
		memset (&candidate->muzzle, 0, sizeof (candidate->muzzle));
	}

	candidate->entity = entity;
	candidate->model = selection->target_model;
	candidate->geometry = selection->target_geometry;
	candidate->joint_count = (uint32_t)target_skeleton->joint_count;
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
	if (candidate->tracked_cull_valid && candidate->attached_prop_valid &&
		candidate->tracked_cull_local_bound < candidate->attached_prop_local_bound)
		candidate->tracked_cull_local_bound = candidate->attached_prop_local_bound;
	return candidate->joint_count != 0;
}

static qboolean R_VRIKRenderEntryCurrent (r_vrik_frame_entry_t *entry)
{
	const entity_t *entity;
	const int slot = entry ? entry->owner_slot : -1;
	int current_id;
	qboolean qbj3_game;

	if (!entry || !cl.entities || slot < 0 || slot >= MAX_SCOREBOARD ||
		slot >= cl.maxclients || slot + 1 >= cl.num_entities || !entry->entity)
		return false;
	entity = entry->entity;
	qbj3_game = R_VRIKRenderQBJ3Game ();
	if (entry->corpse)
	{
		uintptr_t address;
		if (!qbj3_game || cl.maxclients < 1 || cl.maxclients >= cl.num_entities ||
			cl.num_entities <= cl.maxclients + 1)
			return false;
		address = (uintptr_t)entity;
		if (address < (uintptr_t)&cl.entities[cl.maxclients + 1] ||
			address >= (uintptr_t)&cl.entities[cl.num_entities] ||
			R_VRIKRenderQBJ3CorpseOwner (entity) != slot ||
			!R_VRIKRenderExplicitAvatarForSlot (slot, &current_id) ||
			current_id != entry->selected_id)
			return false;
	}
	else
	{
		if (entity != &cl.entities[slot + 1])
			return false;
		current_id = cl.avatar_ids[slot];
		if (current_id != entry->selected_id ||
			(!cl.avatar_custom_keys[slot][0]) != entry->custom_key_empty)
			return false;
		if (current_id > PLAYER_AVATAR_RANGER &&
			(!R_VRIKRenderExplicitAvatarForSlot (slot, &current_id) ||
			 current_id != entry->selected_id))
			return false;
	}
	if (!R_VRIKRenderOriginalModelEligible (entity))
		return false;
	if (entry->selection_valid)
	{
		if (entry->selection.entity != entity)
			return false;
		if (entry->selection.implicit_qbj3)
		{
			if (entry->corpse || !qbj3_game || current_id != PLAYER_AVATAR_RANGER ||
				cl.avatar_custom_keys[slot][0] ||
				CustomAvatar_IdForKey ("qbj3") != entry->selection.id ||
				!R_VRIKRenderQBJ3LivePlayer (entity))
				return false;
		}
		else if (entry->selection.id != entry->selected_id)
			return false;
	}
	entry->qbj3_death = qbj3_game && R_VRIKRenderQBJ3Death (entity);
	return true;
}

void R_VRIKRenderPrepareFrame (uint32_t frame_slot)
{
	size_t candidate_count = 0;
	size_t total_joints = 0;
	VkDeviceSize max_range = vulkan_globals.device_properties.limits.maxStorageBufferRange;
	rs_avatarretarget_us = 0.0;
	rs_avatarretarget_count = 0;

	if (frame_slot >= DOUBLE_BUFFERED)
	{
		R_VRIKRenderInvalidatePublication ();
		return;
	}
	active_frame_slot = frame_slot;
	active_frame_valid = false;
	frame_entries.prepared_count[frame_slot] = 0;
	if (!cl.entities || !frame_entries.count)
	{
		R_VRIKRenderReleaseDescriptorSet (frame_slot);
		active_frame_valid = true;
		return;
	}

	for (size_t i = 0; i < frame_entries.count; ++i)
	{
		r_vrik_frame_entry_t *entry = &frame_entries.entries[i];
		r_vrik_candidate_t candidate = {0};
		if (!R_VRIKRenderEntryCurrent (entry))
			continue;
		const qboolean alternate = entry->selection_valid &&
			R_VRIKRenderAlternateCandidate (entry->entity, &entry->selection,
				&candidate, entry->palette);
		if (!alternate && (entry->corpse || entry->qbj3_death))
			continue;
		if (!alternate && !R_VRIKRenderCandidate (entry->entity, &candidate,
			entry->palette))
			continue;
		const VkDeviceSize palette_bytes = (VkDeviceSize)candidate.joint_count * sizeof (float[12]);
		if (palette_bytes > max_range || total_joints > UINT32_MAX - candidate.joint_count ||
			total_joints > max_range / sizeof (float[12]))
			continue;
		const VkDeviceSize used_bytes = (VkDeviceSize)total_joints * sizeof (float[12]);
		if (palette_bytes > max_range - used_bytes)
			continue;
		if (!entry->corpse && !entry->qbj3_death &&
			entry->owner_slot >= 0 && entry->owner_slot < MAX_SCOREBOARD)
		{
			candidate.muzzle.original_model = entry->entity->model;
			candidate.muzzle.avatar_id = cl.avatar_ids[entry->owner_slot];
			candidate.muzzle.generation = entry->entity->vrik_generation;
			candidate.muzzle.tracking_flags = entry->entity->vrik_poses[0].flags & VRIK_FLAG_KNOWN;
			candidate.muzzle.time = realtime;
		}
		entry->candidate = candidate;
		if (candidate_count != i)
		{
			frame_entries.entries[candidate_count] = *entry;
			R_VRIKRenderRebindSelection (&frame_entries.entries[candidate_count].selection);
		}
		candidate_count++;
		total_joints += candidate.joint_count;
	}
	frame_entries.count = candidate_count;

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
		r_vrik_frame_entry_t *entry = &frame_entries.entries[i];
		r_vrik_candidate_t *candidate = &entry->candidate;
		float (*palette)[12] = (float (*)[12])(storage + joint_cursor * sizeof (float[12]));
		memcpy (palette, entry->palette, candidate->joint_count * sizeof (float[12]));
		r_vrik_prepared_palette_t *record = &entry->prepared[frame_slot];
		memset (record, 0, sizeof (*record));
		record->entity = candidate->entity;
		record->model = candidate->model;
		record->geometry = candidate->geometry;
		record->alternate_avatar = candidate->alternate_avatar;
		record->attached_prop_geometry = candidate->attached_prop_geometry;
		record->attached_prop_valid = candidate->attached_prop_valid;
		memcpy(record->attached_prop_to_canonical,
			candidate->attached_prop_to_canonical,
			sizeof(record->attached_prop_to_canonical));
		if (candidate->alternate_avatar)
			memcpy (record->target_to_canonical, candidate->target_to_canonical,
				sizeof (record->target_to_canonical));
		else
			memset (record->target_to_canonical, 0, sizeof (record->target_to_canonical));
		record->descriptor_set = palette_descriptor_sets[frame_slot];
		record->joint_offset = (uint32_t)joint_cursor;
		record->joint_count = candidate->joint_count;
		record->palette_address = vulkan_globals.ray_query ? allocation_address + joint_cursor * sizeof (float[12]) : 0;
		record->tracked_root_yaw = candidate->tracked_root_yaw;
		record->tracked_root_valid = candidate->tracked_root_valid;
		record->tracked_cull_local_bound = candidate->tracked_cull_local_bound;
		VectorCopy (candidate->tracked_cull_origin, record->tracked_cull_origin);
		record->tracked_cull_valid = candidate->tracked_cull_valid;
		record->muzzle = candidate->muzzle;
		joint_cursor += candidate->joint_count;
		frame_entries.prepared_count[frame_slot] = i + 1;
	}

	if (!frame_entries.prepared_count[frame_slot])
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
	for (size_t i = 0; i < frame_entries.prepared_count[active_frame_slot]; ++i)
		if (frame_entries.entries[i].prepared[active_frame_slot].entity == entity)
			return &frame_entries.entries[i].prepared[active_frame_slot];
	return NULL;
}

void R_VRIKRenderInvalidateMuzzle (const entity_t *entity)
{
	if (!active_frame_valid || !entity || active_frame_slot >= DOUBLE_BUFFERED)
		return;
	for (size_t i = 0; i < frame_entries.prepared_count[active_frame_slot]; ++i)
		if (frame_entries.entries[i].prepared[active_frame_slot].entity == entity)
		{
			frame_entries.entries[i].prepared[active_frame_slot].muzzle.valid = false;
			return;
		}
}

qboolean R_VRIKRenderGetMuzzleOrigin (const entity_t *entity, qboolean discontinuity, vec3_t origin)
{
	if (!active_frame_valid || !entity || !origin || !cl.entities || active_frame_slot >= DOUBLE_BUFFERED)
		return false;
	for (size_t i = 0; i < frame_entries.prepared_count[active_frame_slot]; ++i)
	{
		r_vrik_prepared_palette_t *record =
			&frame_entries.entries[i].prepared[active_frame_slot];
		if (record->entity != entity)
			continue;
		r_vrik_prepared_muzzle_t *muzzle = &record->muzzle;
		if (!muzzle->valid)
			return false;
		int player;
		for (player = 1; player <= cl.maxclients && player < cl.num_entities && player <= MAX_SCOREBOARD; ++player)
			if (entity == &cl.entities[player])
				break;
		const double age = realtime - muzzle->time;
		const double pose_age = realtime - entity->vrik_pose_times[0];
		if (discontinuity || player > cl.maxclients || player >= cl.num_entities || player > MAX_SCOREBOARD ||
			!entity->model || entity->model->needload || entity->model != muzzle->original_model ||
			cl.avatar_ids[player - 1] != muzzle->avatar_id || entity->vrik_generation != muzzle->generation ||
			entity->vrik_slot_retired || entity->vrik_pose_count < 1 ||
			(entity->vrik_poses[0].flags & VRIK_FLAG_KNOWN) != muzzle->tracking_flags ||
			!isfinite (age) || age < 0.0 || age >= 0.25 ||
			!isfinite (pose_age) || pose_age < 0.0 || pose_age > VRIK_POSE_STALE_TIME)
		{
			muzzle->valid = false;
			return false;
		}
		VectorCopy (muzzle->origin, origin);
		return true;
	}
	return false;
}

void R_VRIKRenderShutdown (void)
{
	R_VRIKRenderResetAdmission ();
	active_frame_valid = false;
	active_frame_slot = 0;
	for (int slot = 0; slot < DOUBLE_BUFFERED; ++slot)
	{
		frame_entries.prepared_count[slot] = 0;
		R_VRIKRenderReleaseDescriptorSet (slot);
	}
	Mem_Free (frame_entries.entries);
	memset (&frame_entries, 0, sizeof (frame_entries));
}
