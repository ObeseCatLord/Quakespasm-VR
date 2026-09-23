/* Frame-owned Vulkan palette uploads for the inherited Ranger VRIK solve. */
#include "quakedef.h"
#include "r_vrik.h"
#include "r_vrik_render.h"

#include <limits.h>

#define R_VRIK_RENDER_MAX_JOINTS 256

typedef struct r_vrik_candidate_s
{
	const entity_t *entity;
	const qmodel_t *model;
	md5_skeleton_view_t skeleton;
	lerpdata_t lerpdata;
	vrik_pose_t pose;
	uint32_t joint_count;
	qboolean muzzleflash;
} r_vrik_candidate_t;

static r_vrik_candidate_t candidates[MAX_SCOREBOARD];
static float candidate_palettes[MAX_SCOREBOARD][R_VRIK_RENDER_MAX_JOINTS][12];
static r_vrik_prepared_palette_t prepared[DOUBLE_BUFFERED][MAX_SCOREBOARD];
static size_t prepared_count[DOUBLE_BUFFERED];
static VkDescriptorSet palette_descriptor_sets[DOUBLE_BUFFERED];
static uint32_t active_frame_slot;
static qboolean active_frame_valid;

static void R_VRIKRenderReleaseDescriptorSet (uint32_t frame_slot)
{
	if (palette_descriptor_sets[frame_slot] != VK_NULL_HANDLE)
	{
		R_FreeDescriptorSet (palette_descriptor_sets[frame_slot], &vulkan_globals.joints_buffer_set_layout);
		palette_descriptor_sets[frame_slot] = VK_NULL_HANDLE;
	}
}

static qboolean R_VRIKRenderCandidate (const entity_t *entity, r_vrik_candidate_t *candidate, float (*palette)[12])
{
	aliashdr_t *header;
	r_vrik_palette_output_t output;

	if (!entity || !candidate || !palette)
		return false;
	if (!R_VRIKSampleEntityPose (entity, &candidate->pose))
		return false;
	if (!entity->model || entity->model->type != mod_alias)
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
	output.matrices = palette;
	output.capacity = R_VRIK_RENDER_MAX_JOINTS;
	output.joint_count = 0;
	if (R_VRIKBuildRangerPalette (
			&candidate->skeleton, &candidate->lerpdata, &candidate->pose,
			candidate->muzzleflash, &output) != R_VRIK_PALETTE_OK ||
		output.joint_count > UINT32_MAX)
		return false;

	candidate->entity = entity;
	candidate->model = entity->model;
	candidate->joint_count = (uint32_t)output.joint_count;
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
		if (!R_VRIKRenderCandidate (&cl.entities[player], &candidate, candidate_palettes[candidate_count]))
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
		record->descriptor_set = palette_descriptor_sets[frame_slot];
		record->joint_offset = (uint32_t)joint_cursor;
		record->joint_count = candidate->joint_count;
		record->palette_address = vulkan_globals.ray_query ? allocation_address + joint_cursor * sizeof (float[12]) : 0;
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
	active_frame_valid = false;
	active_frame_slot = 0;
	for (int slot = 0; slot < DOUBLE_BUFFERED; ++slot)
	{
		prepared_count[slot] = 0;
		R_VRIKRenderReleaseDescriptorSet (slot);
	}
}
