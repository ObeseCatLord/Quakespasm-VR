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
// gl_mesh.c: triangle model functions

#include "quakedef.h"
#include "gl_heap.h"
#include "r_vrik_render.h"

#include <math.h>

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

// Heap
#define MESH_HEAP_SIZE_MB	16
#define MESH_HEAP_PAGE_SIZE 4096
#define MESH_HEAP_NAME		"Mesh heap"

extern cvar_t r_lerpmodels;
extern cvar_t r_rtshadows;
extern qmodel_t mod_known[MAX_MODELS];
extern int mod_numknown;

typedef struct entity_blas_surface_s
{
	aliashdr_t		 *geometry;
	VkDeviceAddress vertex_buffer_address;
	VkDeviceAddress index_buffer_address;
	VkDeviceAddress joints_buffer_address;
	int			  numverts_vbo;
	int			  numtris;
	int			  numindexes;
	int			  numposes;
	int			  numframes;
	int			  numjoints;
	int			  poseverttype;
} entity_blas_surface_t;

static qboolean R_EntityBLASCollectSurfaces (
	aliashdr_t *root, const r_vrik_prepared_palette_t *tracked_palette, entity_blas_surface_t *surfaces, uint32_t *surface_count)
{
	if (!root || !surfaces || !surface_count)
		return false;

	const qboolean root_is_md5 = root->poseverttype == PV_MD5 || root->poseverttype == PV_MD5_8;
	if ((root_is_md5 && root->numjoints <= 0) ||
		(tracked_palette && (!root_is_md5 || tracked_palette->joint_count != (uint32_t)root->numjoints)))
		return false;

	const VkPhysicalDeviceAccelerationStructurePropertiesKHR *as_properties = &vulkan_globals.physical_device_acceleration_structure_properties;
	uint32_t count = 0;
	uint64_t total_primitives = 0;
	for (aliashdr_t *hdr = root; hdr; hdr = hdr->nextsurface)
	{
		if (count >= MAX_SURFACES || count >= as_properties->maxGeometryCount || hdr->numverts_vbo <= 0 || hdr->numtris <= 0 ||
			(uint64_t)hdr->numtris > as_properties->maxPrimitiveCount - total_primitives || hdr->numindexes <= 0 ||
			(int64_t)hdr->numtris * 3 != hdr->numindexes || hdr->numframes <= 0 || hdr->numposes <= 0 ||
			!hdr->vertex_buffer_address || !hdr->index_buffer_address)
			return false;
		total_primitives += (uint64_t)hdr->numtris;

		const qboolean is_md5 = hdr->poseverttype == PV_MD5 || hdr->poseverttype == PV_MD5_8;
		if (root_is_md5)
		{
			if (!is_md5 || hdr->numjoints != root->numjoints || hdr->numframes != root->numframes || hdr->numposes != root->numposes ||
				(!tracked_palette && !hdr->joints_buffer_address))
				return false;
		}
		else if (is_md5 || hdr->poseverttype != root->poseverttype || hdr->numframes != root->numframes || hdr->numposes != root->numposes)
			return false;

		entity_blas_surface_t *surface = &surfaces[count++];
		memset (surface, 0, sizeof (*surface));
		surface->geometry = hdr;
		surface->vertex_buffer_address = hdr->vertex_buffer_address;
		surface->index_buffer_address = hdr->index_buffer_address;
		surface->joints_buffer_address = hdr->joints_buffer_address;
		surface->numverts_vbo = hdr->numverts_vbo;
		surface->numtris = hdr->numtris;
		surface->numindexes = hdr->numindexes;
		surface->numposes = hdr->numposes;
		surface->numframes = hdr->numframes;
		surface->numjoints = hdr->numjoints;
		surface->poseverttype = hdr->poseverttype;
	}

	if (count == 0)
		return false;
	*surface_count = count;
	return true;
}

static const r_vrik_prepared_palette_t *R_EntityBLASPalette (const entity_t *e, const aliashdr_t *geometry)
{
	const r_vrik_prepared_palette_t *prepared = R_VRIKRenderLookup (e);
	if (!prepared || !e || !e->model || !geometry || !prepared->model || prepared->geometry != geometry ||
		prepared->descriptor_set == VK_NULL_HANDLE || !prepared->palette_address ||
		(geometry->poseverttype != PV_MD5 && geometry->poseverttype != PV_MD5_8) ||
		geometry->numjoints <= 0 || prepared->joint_count != (uint32_t)geometry->numjoints ||
		geometry->numverts_vbo <= 0 || geometry->numtris <= 0 || !geometry->vertex_buffer_address || !geometry->index_buffer_address)
		return NULL;
	if (prepared->alternate_avatar)
	{
		/* The alternate is an admitted MD5 mesh. The original player entity owns
		 * its BLAS, while the BLAS records the selected model and geometry. */
		if (strcmp (e->model->name, "progs/player.mdl") || prepared->model == e->model ||
			!Mod_IsAdmittedAvatarModel (prepared->model) ||
			geometry != (const aliashdr_t *)prepared->model->extradata[PV_MD5])
			return NULL;
		for (const aliashdr_t *surface = geometry; surface; surface = surface->nextsurface)
			if ((surface->poseverttype != PV_MD5 && surface->poseverttype != PV_MD5_8) ||
				surface->numjoints != (int)prepared->joint_count)
				return NULL;
		for (int i = 0; i < 12; ++i)
			if (!isfinite (prepared->target_to_canonical[i]))
				return NULL;
	}
	else if (prepared->model != e->model ||
		geometry != (const aliashdr_t *)Mod_Extradata_CheckSkin (e->model, e->skinnum))
		return NULL;
	return prepared;
}

static aliashdr_t *R_EntityBLASGeometry (entity_t *e, qboolean allow_tracked_palette)
{
	const r_vrik_prepared_palette_t *prepared;
	aliashdr_t *selected = (aliashdr_t *)Mod_Extradata_CheckSkin (e->model, e->skinnum);

	if (!allow_tracked_palette)
		return selected;

	prepared = R_VRIKRenderLookup (e);
	if (!prepared)
		return selected;

	if (!selected)
		return NULL;
	aliashdr_t *geometry = prepared->alternate_avatar ? (aliashdr_t *)prepared->geometry : selected;

	/* A visible tracked mesh must have a matching BLAS, never a Ranger stand-in. */
	entity_blas_surface_t surfaces[MAX_SURFACES];
	uint32_t surface_count;
	return R_EntityBLASPalette (e, geometry) &&
			R_EntityBLASCollectSurfaces (geometry, prepared, surfaces, &surface_count) ?
			geometry : NULL;
}

static glheap_t	 *mesh_buffer_heap;
static SDL_Mutex *mesh_mutex;

static qboolean GLMesh_ComputeTrackedCullQmax (const aliashdr_t *hdr, const byte *vertexes, double *out_qmax)
{
	if (!hdr || !vertexes || !out_qmax || hdr->numverts_vbo <= 0 ||
		(hdr->poseverttype != PV_MD5 && hdr->poseverttype != PV_MD5_8))
		return false;

	const int influences = hdr->poseverttype == PV_MD5 ? NUM_JOINT_INFLUENCES_4_WEIGHT : NUM_JOINT_INFLUENCES_8_WEIGHT;
	double max_qsum = 0.0;
	for (int vertex = 0; vertex < hdr->numverts_vbo; ++vertex)
	{
		double qsum = 0.0;
		for (int influence = 0; influence < influences; ++influence)
		{
			double x, y, z;
			if (hdr->poseverttype == PV_MD5)
			{
				const md5vert_t *v = (const md5vert_t *)vertexes + vertex;
				x = v->joint_position_x[influence];
				y = v->joint_position_y[influence];
				z = v->joint_position_z[influence];
			}
			else
			{
				const md5vert8_t *v = (const md5vert8_t *)vertexes + vertex;
				x = v->joint_position_x[influence];
				y = v->joint_position_y[influence];
				z = v->joint_position_z[influence];
			}
			if (!isfinite (x) || !isfinite (y) || !isfinite (z))
				return false;
			qsum += sqrt (x * x + y * y + z * z);
			if (!isfinite (qsum))
				return false;
		}
		if (max_qsum < qsum)
			max_qsum = qsum;
	}
	*out_qmax = max_qsum;
	return true;
}

/* Final bytes are surface-owned; borrowed MD5 poses share model lifetime. */
struct alias_gpu_upload_s
{
	byte *vertices, *indexes, *skeleton_indexes;
	const byte *joints;
	size_t vertex_size, index_size, skeleton_size, joint_size;
	qboolean owns_joints, joints_ready;
};

static void GLMesh_FreeUpload (aliashdr_t *hdr)
{
	alias_gpu_upload_t *upload = hdr->gpu_upload;
	if (!upload)
		return;
	SAFE_FREE (upload->vertices);
	SAFE_FREE (upload->indexes);
	SAFE_FREE (upload->skeleton_indexes);
	if (upload->owns_joints)
		Mem_Free ((void *)upload->joints);
	Mem_Free (upload);
	hdr->gpu_upload = NULL;
}

typedef struct
{
	VkBuffer				  buffer;
	VkDescriptorSet			  descriptor_set;
	glheapallocation_t		 *allocation;
	VkDescriptorSet			  desc_set;
	vulkan_desc_set_layout_t *desc_set_layout;
} buffer_garbage_t;

typedef struct
{
	VkAccelerationStructureKHR blas;
	VkBuffer				   buffer;
	glheapallocation_t		  *allocation;
} blas_garbage_t;

static int				 current_garbage_index;
static int				 num_garbage_buffers[2];
static buffer_garbage_t *buffer_garbage[2];
static int				 num_garbage_blas[2];
static blas_garbage_t	*blas_garbage[2];

/*
================
AddBufferGarbage
================
*/
static void AddBufferGarbage (
	VkBuffer buffer, VkDescriptorSet descriptor_set, glheapallocation_t *allocation, const VkDescriptorSet desc_set, vulkan_desc_set_layout_t *desc_set_layout)
{
	SDL_LockMutex (mesh_mutex);

	int *num_garbage = &num_garbage_buffers[current_garbage_index];
	int	 old_num_garbage = *num_garbage;
	*num_garbage += 1;
	if (buffer_garbage[current_garbage_index] == NULL)
		buffer_garbage[current_garbage_index] = Mem_Alloc (sizeof (buffer_garbage_t) * (*num_garbage));
	else
		buffer_garbage[current_garbage_index] = Mem_Realloc (buffer_garbage[current_garbage_index], sizeof (buffer_garbage_t) * (*num_garbage));
	buffer_garbage_t *garbage = &buffer_garbage[current_garbage_index][old_num_garbage];
	garbage->buffer = buffer;
	garbage->descriptor_set = descriptor_set;
	garbage->allocation = allocation;
	garbage->desc_set = desc_set;
	garbage->desc_set_layout = desc_set_layout;
	SDL_UnlockMutex (mesh_mutex);
}

/*
================
AddBLASGarbage
================
*/
static void AddBLASGarbage (VkAccelerationStructureKHR blas, VkBuffer buffer, glheapallocation_t *allocation)
{
	SDL_LockMutex (mesh_mutex);

	int *num_garbage = &num_garbage_blas[current_garbage_index];
	int	 old_num_garbage = *num_garbage;
	*num_garbage += 1;
	if (blas_garbage[current_garbage_index] == NULL)
		blas_garbage[current_garbage_index] = Mem_Alloc (sizeof (blas_garbage_t) * (*num_garbage));
	else
		blas_garbage[current_garbage_index] = Mem_Realloc (blas_garbage[current_garbage_index], sizeof (blas_garbage_t) * (*num_garbage));
	blas_garbage_t *g = &blas_garbage[current_garbage_index][old_num_garbage];
	g->blas = blas;
	g->buffer = buffer;
	g->allocation = allocation;
	SDL_UnlockMutex (mesh_mutex);
}

/* Retire a private prop's one shared BLAS before any surface input buffer.
 * The model loader also calls mesh deletion after reset, so this is idempotent. */
static void GLMesh_FreeAvatarPropBLAS (aliashdr_t *root)
{
	if (!root || !root->avatar_static_prop)
		return;

	VkAccelerationStructureKHR blas = root->avatar_prop_blas;
	VkBuffer buffer = root->avatar_prop_blas_buffer;
	glheapallocation_t *allocation = root->avatar_prop_blas_allocation;
	root->avatar_prop_blas = VK_NULL_HANDLE;
	root->avatar_prop_blas_buffer = VK_NULL_HANDLE;
	root->avatar_prop_blas_allocation = NULL;
	root->avatar_prop_blas_address = 0;
	root->avatar_prop_blas_built = false;
	root->avatar_prop_blas_failed = false;
	if (blas == VK_NULL_HANDLE)
		return;

	if (in_update_screen)
		AddBLASGarbage (blas, buffer, allocation);
	else
	{
		GL_WaitForDeviceIdle ();
		SDL_LockMutex (mesh_mutex);
		vulkan_globals.vk_destroy_acceleration_structure (vulkan_globals.device, blas, NULL);
		vkDestroyBuffer (vulkan_globals.device, buffer, NULL);
		GL_HeapFree (mesh_buffer_heap, allocation, &num_vulkan_mesh_allocations);
		SDL_UnlockMutex (mesh_mutex);
	}
}

/*
================
R_InitMeshHeap
================
*/
void R_InitMeshHeap (void)
{
	mesh_mutex = SDL_CreateMutex ();

	// Allocate index buffer & upload to GPU
	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = 16;
	buffer_create_info.usage =
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if (vulkan_globals.ray_query)
		buffer_create_info.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
	VkBuffer dummy_buffer;
	VkResult err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &dummy_buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed with code %i", (int)err);

	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, dummy_buffer, &memory_requirements);

	const uint32_t memory_type_index = GL_MemoryTypeFromProperties (memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	VkDeviceSize   heap_size = MESH_HEAP_SIZE_MB * (VkDeviceSize)1024 * (VkDeviceSize)1024;
	mesh_buffer_heap = GL_HeapCreate (heap_size, MESH_HEAP_PAGE_SIZE, memory_type_index, VULKAN_MEMORY_TYPE_DEVICE, vulkan_globals.ray_query, MESH_HEAP_NAME);

	vkDestroyBuffer (vulkan_globals.device, dummy_buffer, NULL);
}

/*
================
R_GetMeshHeapStats
================
*/
glheapstats_t R_GetMeshHeapStats (void)
{
	SDL_LockMutex (mesh_mutex);
	glheapstats_t stats = *GL_HeapGetStats (mesh_buffer_heap);
	SDL_UnlockMutex (mesh_mutex);
	return stats;
}

/*
================
R_CollectMeshBufferGarbage
================
*/
void R_CollectMeshBufferGarbage (void)
{
	SDL_LockMutex (mesh_mutex);

	current_garbage_index = (current_garbage_index + 1) % 2;

	if (num_garbage_buffers[current_garbage_index] > 0)
	{
		for (int i = 0; i < num_garbage_buffers[current_garbage_index]; ++i)
		{
			buffer_garbage_t *garbage = &buffer_garbage[current_garbage_index][i];
			vkDestroyBuffer (vulkan_globals.device, garbage->buffer, NULL);
			GL_HeapFree (mesh_buffer_heap, garbage->allocation, &num_vulkan_mesh_allocations);
			if (garbage->desc_set != VK_NULL_HANDLE)
				R_FreeDescriptorSet (garbage->desc_set, garbage->desc_set_layout);
		}
		Mem_Free (buffer_garbage[current_garbage_index]);
		buffer_garbage[current_garbage_index] = NULL;
		num_garbage_buffers[current_garbage_index] = 0;
	}

	if (num_garbage_blas[current_garbage_index] > 0)
	{
		for (int i = 0; i < num_garbage_blas[current_garbage_index]; ++i)
		{
			blas_garbage_t *blas_g = &blas_garbage[current_garbage_index][i];
			vulkan_globals.vk_destroy_acceleration_structure (vulkan_globals.device, blas_g->blas, NULL);
			vkDestroyBuffer (vulkan_globals.device, blas_g->buffer, NULL);
			GL_HeapFree (mesh_buffer_heap, blas_g->allocation, &num_vulkan_mesh_allocations);
		}
		Mem_Free (blas_garbage[current_garbage_index]);
		blas_garbage[current_garbage_index] = NULL;
		num_garbage_blas[current_garbage_index] = 0;
	}
	SDL_UnlockMutex (mesh_mutex);
}

/*
================
GL_MakeAliasModelDisplayLists
Original code by MH from RMQEngine
================
*/
static uint32_t AliasMeshHash (const void *const p)
{
	aliasmesh_t *mesh = (aliasmesh_t *)p;
	uint32_t	 vertindex = mesh->vertindex;
	return HashCombine (HashInt32 (&vertindex), HashCombine (HashFloat (&mesh->st[0]), HashFloat (&mesh->st[1])));
}

static qboolean GLMesh_CheckedSizeMul (size_t a, size_t b, size_t *result)
{
	if (a && b > SIZE_MAX / a)
		return false;
	*result = a * b;
	return true;
}

static qboolean GLMesh_CheckedSizeAdd (size_t a, size_t b, size_t *result)
{
	if (b > SIZE_MAX - a)
		return false;
	*result = a + b;
	return true;
}

void GL_MakeAliasModelDisplayLists (qmodel_t *m, aliashdr_t *paliashdr)
{
	assert (paliashdr->poseverttype == PV_QUAKE1);

	Con_DPrintf2 ("meshing %s...\n", m->name);
	if (paliashdr->numposes <= 0 || paliashdr->numverts <= 0 || paliashdr->numtris < 0 ||
		paliashdr->numtris > INT_MAX / 3)
		Sys_Error ("Alias model %s has invalid mesh dimensions", m->name);

	size_t pose_vertex_count;
	if (!GLMesh_CheckedSizeMul ((size_t)paliashdr->numposes, (size_t)paliashdr->numverts, &pose_vertex_count) ||
		pose_vertex_count > SIZE_MAX / sizeof (trivertx_t))
		Sys_Error ("Alias model %s pose vertex data is too large", m->name);

	// first, copy the verts onto the hunk
	TEMP_ALLOC_ZEROED (trivertx_t, verts, pose_vertex_count);

	for (int i = 0; i < paliashdr->numposes; i++)
		for (int j = 0; j < paliashdr->numverts; j++)
			verts[(size_t)i * (size_t)paliashdr->numverts + (size_t)j] = poseverts[i][j];

	// there can never be more than this number of verts
	const int maxverts_vbo = paliashdr->numtris * 3;
	if ((size_t)maxverts_vbo > SIZE_MAX / sizeof (aliasmesh_t) ||
		(size_t)maxverts_vbo > SIZE_MAX / sizeof (unsigned short))
		Sys_Error ("Alias model %s display list is too large", m->name);
	TEMP_ALLOC_ZEROED (aliasmesh_t, desc, maxverts_vbo);
	// there will always be this number of indexes
	TEMP_ALLOC_ZEROED (unsigned short, indexes, maxverts_vbo);

	hash_map_t *vertex_to_index_map = HashMap_Create (aliasmesh_t, unsigned short, &AliasMeshHash, NULL);
	HashMap_Reserve (vertex_to_index_map, maxverts_vbo);

	ZEROED_STRUCT (aliasmesh_t, mesh);
	for (int i = 0; i < paliashdr->numtris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			// index into hdr->vertexes
			int raw_vertindex = triangles[i].vertindex[j];
			if (raw_vertindex < 0 || raw_vertindex >= paliashdr->numverts)
				Sys_Error ("Alias model %s has an invalid triangle vertex index", m->name);
			unsigned short vertindex = (unsigned short)raw_vertindex;

			// basic s/t coords
			int s = stverts[vertindex].s;
			int t = stverts[vertindex].t;

			// check for back side and adjust texcoord s
			if (!triangles[i].facesfront && stverts[vertindex].onseam)
				s += paliashdr->skinwidth / 2;

			mesh.st[0] = s;
			mesh.st[1] = t;
			mesh.vertindex = vertindex;

			// Check if this vert already exists
			unsigned short	index;
			unsigned short *found_index;
			if ((found_index = HashMap_Lookup (unsigned short, vertex_to_index_map, &mesh)))
				index = *found_index;
			else
			{
				// doesn't exist; emit a new vert and index
				if (paliashdr->numverts_vbo > UINT16_MAX)
					Sys_Error ("Alias model %s has too many display-list vertices for 16-bit indexes", m->name);
				index = paliashdr->numverts_vbo;
				HashMap_Insert (vertex_to_index_map, &mesh, &index);
				desc[paliashdr->numverts_vbo].vertindex = vertindex;
				desc[paliashdr->numverts_vbo].st[0] = s;
				desc[paliashdr->numverts_vbo++].st[1] = t;
			}

			indexes[paliashdr->numindexes++] = index;
		}
	}

	HashMap_Destroy (vertex_to_index_map);

	// upload immediately
	paliashdr->poseverttype = PV_QUAKE1;
	GLMesh_UploadBuffers (m, paliashdr, indexes, (byte *)verts, desc, NULL, NULL, 0);

	TEMP_FREE (indexes);
	TEMP_FREE (desc);
	TEMP_FREE (verts);
}

#define NUMVERTEXNORMALS 162
extern float r_avertexnormals[NUMVERTEXNORMALS][3];

/*
================
GLMesh_DeleteMeshBuffers
================
*/
static void GLMesh_DeleteMeshBuffersInternal (aliashdr_t *mainhdr, qboolean dispose_cpu)
{
	GLMesh_FreeAvatarPropBLAS (mainhdr);
	// Delete all surfaces:
	for (aliashdr_t *hdr = mainhdr; hdr != NULL; hdr = hdr->nextsurface)
	{
		if (in_update_screen)
		{
			if (hdr->vertex_buffer != VK_NULL_HANDLE)
				AddBufferGarbage (hdr->vertex_buffer, VK_NULL_HANDLE, hdr->vertex_allocation, VK_NULL_HANDLE, NULL);
			if (hdr->index_buffer != VK_NULL_HANDLE)
				AddBufferGarbage (hdr->index_buffer, VK_NULL_HANDLE, hdr->index_allocation, VK_NULL_HANDLE, NULL);
			if (hdr->skeleton_index_buffer != VK_NULL_HANDLE)
				AddBufferGarbage (hdr->skeleton_index_buffer, VK_NULL_HANDLE, hdr->skeleton_index_allocation, VK_NULL_HANDLE, NULL);
			if (hdr->joints_buffer != VK_NULL_HANDLE)
				AddBufferGarbage (hdr->joints_buffer, VK_NULL_HANDLE, hdr->joints_allocation, hdr->joints_set, &vulkan_globals.joints_buffer_set_layout);
		}
		else if (hdr->vertex_buffer != VK_NULL_HANDLE || hdr->index_buffer != VK_NULL_HANDLE ||
			hdr->skeleton_index_buffer != VK_NULL_HANDLE || hdr->joints_buffer != VK_NULL_HANDLE)
		{
			GL_WaitForDeviceIdle ();
			SDL_LockMutex (mesh_mutex);

			if (hdr->vertex_buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer (vulkan_globals.device, hdr->vertex_buffer, NULL);
				GL_HeapFree (mesh_buffer_heap, hdr->vertex_allocation, &num_vulkan_mesh_allocations);
			}

			if (hdr->index_buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer (vulkan_globals.device, hdr->index_buffer, NULL);
				GL_HeapFree (mesh_buffer_heap, hdr->index_allocation, &num_vulkan_mesh_allocations);
			}

			if (hdr->skeleton_index_buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer (vulkan_globals.device, hdr->skeleton_index_buffer, NULL);
				GL_HeapFree (mesh_buffer_heap, hdr->skeleton_index_allocation, &num_vulkan_mesh_allocations);
			}

			if (hdr->joints_buffer != VK_NULL_HANDLE)
			{
				vkDestroyBuffer (vulkan_globals.device, hdr->joints_buffer, NULL);
				GL_HeapFree (mesh_buffer_heap, hdr->joints_allocation, &num_vulkan_mesh_allocations);
				R_FreeDescriptorSet (hdr->joints_set, &vulkan_globals.joints_buffer_set_layout);
			}
			SDL_UnlockMutex (mesh_mutex);
		}

		hdr->vertex_buffer = VK_NULL_HANDLE;
		hdr->vertex_allocation = NULL;
		hdr->vertex_buffer_address = 0;
		hdr->index_buffer = VK_NULL_HANDLE;
		hdr->index_allocation = NULL;
		hdr->index_buffer_address = 0;
		hdr->skeleton_index_buffer = VK_NULL_HANDLE;
		hdr->skeleton_index_allocation = NULL;
		hdr->joints_buffer = VK_NULL_HANDLE;
		hdr->joints_allocation = NULL;
		hdr->joints_buffer_address = 0;
		hdr->joints_set = VK_NULL_HANDLE;
		if (dispose_cpu)
		{
			for (int i = 0; i < MAX_SKINS; ++i)
				SAFE_FREE (hdr->texels[i]);
			GLMesh_FreeUpload (hdr);
		}
	}
}

void GLMesh_DeleteMeshBuffers (aliashdr_t *mainhdr)
{
	GLMesh_DeleteMeshBuffersInternal (mainhdr, true);
}

/*
================
GLMesh_UploadBuffers : Upload data for a single aliashdr_t *hdr (not it's nextsurfaces)
================
*/
/* Initial creation and replay share all Vulkan buffer policy and allocation. */
static void GLMesh_CreateBuffersFromUpload (qmodel_t *mod, aliashdr_t *hdr)
{
	const alias_gpu_upload_t *upload = hdr->gpu_upload;
	const size_t totalindexsize = upload->index_size;
	const size_t skeleton_index_size = upload->skeleton_size;
	const size_t totalvbosize = upload->vertex_size;
	const size_t totaljointssize = upload->joint_size;
	const byte *indexes = upload->indexes;
	const byte *skeleton_indexes = upload->skeleton_indexes;
	byte *vbodata = upload->vertices;
	const byte *joints = upload->joints;
	const int num_skeleton_indexes = hdr->num_skeleton_indexes;
	VkResult err;

	{
		// Allocate index buffer & upload to GPU
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = totalindexsize;
		buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (vulkan_globals.ray_query)
			buffer_create_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &hdr->index_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)hdr->index_buffer, VK_OBJECT_TYPE_BUFFER, mod->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, hdr->index_buffer, &memory_requirements);

		SDL_LockMutex (mesh_mutex);
		hdr->index_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		SDL_UnlockMutex (mesh_mutex);
		err = vkBindBufferMemory (
			vulkan_globals.device, hdr->index_buffer, GL_HeapGetAllocationMemory (hdr->index_allocation), GL_HeapGetAllocationOffset (hdr->index_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed with code %i", (int)err);

		R_StagingUploadBuffer (hdr->index_buffer, totalindexsize, (byte *)indexes);

		// Get device address for ray tracing
		if (vulkan_globals.ray_query)
		{
			ZEROED_STRUCT (VkBufferDeviceAddressInfoKHR, address_info);
			address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
			address_info.buffer = hdr->index_buffer;
			hdr->index_buffer_address = vulkan_globals.vk_get_buffer_device_address (vulkan_globals.device, &address_info);
		}
	}

	if (skeleton_indexes && num_skeleton_indexes > 0)
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = skeleton_index_size;
		buffer_create_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &hdr->skeleton_index_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)hdr->skeleton_index_buffer, VK_OBJECT_TYPE_BUFFER, mod->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, hdr->skeleton_index_buffer, &memory_requirements);

		SDL_LockMutex (mesh_mutex);
		hdr->skeleton_index_allocation =
			GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		SDL_UnlockMutex (mesh_mutex);
		err = vkBindBufferMemory (
			vulkan_globals.device, hdr->skeleton_index_buffer, GL_HeapGetAllocationMemory (hdr->skeleton_index_allocation),
			GL_HeapGetAllocationOffset (hdr->skeleton_index_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed with code %i", (int)err);

		R_StagingUploadBuffer (hdr->skeleton_index_buffer, skeleton_index_size, (byte *)skeleton_indexes);
	}

	// Allocate vertex buffer & upload to GPU
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = totalvbosize;
		buffer_create_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		if (vulkan_globals.ray_query)
		{
			buffer_create_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			if (hdr->avatar_static_prop)
				buffer_create_info.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
		}
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &hdr->vertex_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)hdr->vertex_buffer, VK_OBJECT_TYPE_BUFFER, mod->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, hdr->vertex_buffer, &memory_requirements);

		SDL_LockMutex (mesh_mutex);
		hdr->vertex_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		SDL_UnlockMutex (mesh_mutex);
		err = vkBindBufferMemory (
			vulkan_globals.device, hdr->vertex_buffer, GL_HeapGetAllocationMemory (hdr->vertex_allocation),
			GL_HeapGetAllocationOffset (hdr->vertex_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed with code %i", (int)err);

		R_StagingUploadBuffer (hdr->vertex_buffer, totalvbosize, vbodata);

		// Get device address for ray tracing
		if (vulkan_globals.ray_query)
		{
			ZEROED_STRUCT (VkBufferDeviceAddressInfoKHR, address_info);
			address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
			address_info.buffer = hdr->vertex_buffer;
			hdr->vertex_buffer_address = vulkan_globals.vk_get_buffer_device_address (vulkan_globals.device, &address_info);
		}
	}

	// Allocate joints buffer & upload to GPU
	if (joints)
	{
		ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
		buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_create_info.size = totaljointssize;
		buffer_create_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		if (vulkan_globals.ray_query)
			buffer_create_info.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &hdr->joints_buffer);
		if (err != VK_SUCCESS)
			Sys_Error ("vkCreateBuffer failed with code %i", (int)err);

		GL_SetObjectName ((uint64_t)hdr->joints_buffer, VK_OBJECT_TYPE_BUFFER, mod->name);

		VkMemoryRequirements memory_requirements;
		vkGetBufferMemoryRequirements (vulkan_globals.device, hdr->joints_buffer, &memory_requirements);

		SDL_LockMutex (mesh_mutex);
		hdr->joints_allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
		SDL_UnlockMutex (mesh_mutex);
		err = vkBindBufferMemory (
			vulkan_globals.device, hdr->joints_buffer, GL_HeapGetAllocationMemory (hdr->joints_allocation),
			GL_HeapGetAllocationOffset (hdr->joints_allocation));
		if (err != VK_SUCCESS)
			Sys_Error ("vkBindBufferMemory failed with code %i", (int)err);

		R_StagingUploadBuffer (hdr->joints_buffer, totaljointssize, (byte *)joints);

		// Get device address for ray tracing
		if (vulkan_globals.ray_query)
		{
			ZEROED_STRUCT (VkBufferDeviceAddressInfoKHR, address_info);
			address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR;
			address_info.buffer = hdr->joints_buffer;
			hdr->joints_buffer_address = vulkan_globals.vk_get_buffer_device_address (vulkan_globals.device, &address_info);
		}

		hdr->joints_set = R_AllocateDescriptorSet (&vulkan_globals.joints_buffer_set_layout);

		ZEROED_STRUCT (VkDescriptorBufferInfo, buffer_info);
		buffer_info.buffer = hdr->joints_buffer;
		buffer_info.offset = 0;
		buffer_info.range = VK_WHOLE_SIZE;

		ZEROED_STRUCT (VkWriteDescriptorSet, joints_set_write);
		joints_set_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		joints_set_write.dstSet = hdr->joints_set;
		joints_set_write.dstBinding = 0;
		joints_set_write.dstArrayElement = 0;
		joints_set_write.descriptorCount = 1;
		joints_set_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		joints_set_write.pBufferInfo = &buffer_info;

		vkUpdateDescriptorSets (vulkan_globals.device, 1, &joints_set_write, 0, NULL);
	}

}

void GLMesh_UploadBuffers (
	qmodel_t *mod, aliashdr_t *hdr, unsigned short *indexes, byte *vertexes, aliasmesh_t *desc, jointpose_t *joints, unsigned short *skeleton_indexes,
	int num_skeleton_indexes)
{
	size_t	 totalvbosize = 0;
	size_t	 vertex_data_size = 0;
	size_t	 st_data_size = 0;
	size_t	 totalindexsize = 0;
	size_t	 totaljointssize = 0;
	size_t	 skeleton_index_size = 0;
	size_t	 input_vertex_count;
	size_t	 output_vertex_count;
	size_t	 numverts;
	size_t	 numindexes;
	if (!hdr)
		return;
	hdr->tracked_cull_qmax = 0.0;
	hdr->tracked_cull_qmax_valid = false;

	if (hdr->numverts <= 0 || hdr->numverts_vbo <= 0 ||
		(size_t)hdr->numverts > (size_t)UINT16_MAX + 1 || (size_t)hdr->numverts_vbo > (size_t)UINT16_MAX + 1 ||
		hdr->numindexes < 0 || num_skeleton_indexes < 0 ||
		hdr->numframes < 0 || hdr->numjoints < 0)
		Sys_Error ("GLMesh_UploadBuffers: %s has invalid mesh dimensions", mod->name);
	numverts = (size_t)hdr->numverts_vbo;
	numindexes = (size_t)hdr->numindexes;
	if (!GLMesh_CheckedSizeMul (numindexes, sizeof (*indexes), &totalindexsize))
		Sys_Error ("GLMesh_UploadBuffers: %s index buffer is too large", mod->name);
	if (skeleton_indexes && num_skeleton_indexes > 0 &&
		!GLMesh_CheckedSizeMul ((size_t)num_skeleton_indexes, sizeof (*skeleton_indexes), &skeleton_index_size))
		Sys_Error ("GLMesh_UploadBuffers: %s skeleton index buffer is too large", mod->name);

	switch (hdr->poseverttype)
	{
	case PV_QUAKE1:
	{
		if (hdr->numposes <= 0 ||
			!GLMesh_CheckedSizeMul ((size_t)hdr->numverts, (size_t)hdr->numposes, &input_vertex_count) ||
			input_vertex_count > SIZE_MAX / sizeof (trivertx_t) ||
			!GLMesh_CheckedSizeMul (numverts, (size_t)hdr->numposes, &output_vertex_count) ||
			!GLMesh_CheckedSizeMul (output_vertex_count, sizeof (meshxyz_t), &vertex_data_size))
			Sys_Error ("GLMesh_UploadBuffers: %s has invalid or oversized MDL vertex data", mod->name);
	}
	break;
	case PV_QUAKE3:
	{
		if (hdr->numframes <= 0 ||
			!GLMesh_CheckedSizeMul ((size_t)hdr->numverts, (size_t)hdr->numframes, &input_vertex_count) ||
			input_vertex_count > SIZE_MAX / sizeof (md3XyzNormal_t) ||
			!GLMesh_CheckedSizeMul (numverts, (size_t)hdr->numframes, &output_vertex_count) ||
			!GLMesh_CheckedSizeMul (output_vertex_count, sizeof (meshxyz_t), &vertex_data_size))
			Sys_Error ("GLMesh_UploadBuffers: %s has invalid or oversized MD3 vertex data", mod->name);
	}
	break;
	case PV_MD5:
	{
		if (hdr->numposes != 1 ||
			!GLMesh_CheckedSizeMul (numverts, sizeof (md5vert_t), &vertex_data_size))
			Sys_Error ("GLMesh_UploadBuffers: %s has invalid or oversized MD5 vertex data", mod->name);
	}
	break;
	case PV_MD5_8:
	{
		if (hdr->numposes != 1 ||
			!GLMesh_CheckedSizeMul (numverts, sizeof (md5vert8_t), &vertex_data_size))
			Sys_Error ("GLMesh_UploadBuffers: %s has invalid or oversized MD5_8 vertex data", mod->name);
	}
	break;
	default:
		Sys_Error ("GLMesh_UploadBuffers: %s has an invalid pose vertex type", mod->name);
	}

	if (joints)
	{
		size_t joint_pose_count;
		if (hdr->numframes <= 0 || hdr->numjoints <= 0 ||
			!GLMesh_CheckedSizeMul ((size_t)hdr->numframes, (size_t)hdr->numjoints, &joint_pose_count) ||
			!GLMesh_CheckedSizeMul (joint_pose_count, sizeof (jointpose_t), &totaljointssize))
			Sys_Error ("GLMesh_UploadBuffers: %s joint buffer is too large", mod->name);
	}
	else if (hdr->numframes > 0 && hdr->numjoints > 0)
	{
		size_t joint_pose_count;
		if (!GLMesh_CheckedSizeMul ((size_t)hdr->numframes, (size_t)hdr->numjoints, &joint_pose_count) ||
			!GLMesh_CheckedSizeMul (joint_pose_count, sizeof (jointpose_t), &totaljointssize))
			Sys_Error ("GLMesh_UploadBuffers: %s joint data is too large", mod->name);
	}

	if (hdr->poseverttype == PV_QUAKE1 || hdr->poseverttype == PV_QUAKE3)
	{
		// reserve room from ST data starting at vbostofs.
		if (vertex_data_size > INT_MAX ||
			!GLMesh_CheckedSizeMul (numverts, sizeof (meshst_t), &st_data_size) ||
			!GLMesh_CheckedSizeAdd (vertex_data_size, st_data_size, &totalvbosize))
			Sys_Error ("GLMesh_UploadBuffers: %s vertex buffer is too large", mod->name);
	}
	else
		totalvbosize = vertex_data_size;
	if (totalvbosize > INT_MAX)
		Sys_Error ("GLMesh_UploadBuffers: %s vertex buffer exceeds the int offset limit", mod->name);
	if (hdr->poseverttype == PV_QUAKE1 || hdr->poseverttype == PV_QUAKE3)
		hdr->vbostofs = (int)vertex_data_size;

	if (GLMesh_ComputeTrackedCullQmax (hdr, vertexes, &hdr->tracked_cull_qmax))
		hdr->tracked_cull_qmax_valid = true;

	if (isDedicated)
		return;
	if (!numindexes)
		return;
	if (!totalvbosize)
		return;
	if (!indexes || !vertexes ||
		((hdr->poseverttype == PV_QUAKE1 || hdr->poseverttype == PV_QUAKE3) && !desc))
		Sys_Error ("GLMesh_UploadBuffers: %s has missing mesh data", mod->name);
	if (hdr->gpu_upload || hdr->vertex_buffer || hdr->index_buffer ||
		hdr->skeleton_index_buffer || hdr->joints_buffer)
		Sys_Error ("GLMesh_UploadBuffers: %s already owns mesh resources", mod->name);
	hdr->num_skeleton_indexes = num_skeleton_indexes;

	// create the vertex buffer (empty)
	byte *vbodata = Mem_Alloc (totalvbosize);

	// fill in the vertices of the buffer
	size_t vertofs = 0;

	switch (hdr->poseverttype)
	{
	case PV_QUAKE1:
		for (int f = 0; f < hdr->numposes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
		{
			meshxyz_t		 *xyz = (meshxyz_t *)vbodata + vertofs;
			const trivertx_t *tv = (trivertx_t *)vertexes + ((size_t)hdr->numverts * (size_t)f);
			vertofs += hdr->numverts_vbo;

			for (int v = 0; v < hdr->numverts_vbo; v++)
			{
				trivertx_t trivert = tv[desc[v].vertindex];
				// MDL is [0-255] => remapped on unsigned 16bit [0; 65535] seen as [0,1] coords in the vertex shader
				// to be compatible with the MD3 range
				xyz[v].xyz[0] = (int)trivert.v[0] * 257;
				xyz[v].xyz[1] = (int)trivert.v[1] * 257;
				xyz[v].xyz[2] = (int)trivert.v[2] * 257;
				xyz[v].xyz[3] = 1; // need w 1 for 4 byte vertex compression

				// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
				// this introduces some error (less than 0.004), but the normals were very coarse
				// to begin with
				xyz[v].normal[0] = 127 * r_avertexnormals[trivert.lightnormalindex][0];
				xyz[v].normal[1] = 127 * r_avertexnormals[trivert.lightnormalindex][1];
				xyz[v].normal[2] = 127 * r_avertexnormals[trivert.lightnormalindex][2];
				xyz[v].normal[3] = 0; // unused; for 4-byte alignment
			}
		}
		break;
	case PV_QUAKE3:
		for (int f = 0; f < hdr->numframes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
		{
			meshxyz_t			 *xyz = (meshxyz_t *)vbodata + vertofs;
			const md3XyzNormal_t *tv = (md3XyzNormal_t *)vertexes + ((size_t)hdr->numverts * (size_t)f);
			vertofs += hdr->numverts_vbo;

			float lat, lng;

			for (int v = 0; v < hdr->numverts_vbo; v++, tv++)
			{
				// MD3 is SIGNED 16bit => remapped on unsigned 16bit seen as [0,1] coords in the vertex shader
				xyz[v].xyz[0] = (int)tv->xyz[0] + 32768;
				xyz[v].xyz[1] = (int)tv->xyz[1] + 32768;
				xyz[v].xyz[2] = (int)tv->xyz[2] + 32768;
				xyz[v].xyz[3] = 1; // need w 1 for 4 byte vertex compression

				// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
				// this introduces some error (less than 0.004), but the normals were very coarse
				// to begin with
				lat = (float)tv->latlong[0] * (2 * M_PI) * (1.0 / 255.0);
				lng = (float)tv->latlong[1] * (2 * M_PI) * (1.0 / 255.0);
				xyz[v].normal[0] = 127 * cos (lng) * sin (lat);
				xyz[v].normal[1] = 127 * sin (lng) * sin (lat);
				xyz[v].normal[2] = 127 * cos (lat);
				xyz[v].normal[3] = 0; // unused; for 4-byte alignment
			}
		}
		break;
	case PV_MD5:
	case PV_MD5_8:
		memcpy (vbodata, vertexes, totalvbosize);
		// vertexes is already the concat of the hdr surface vertices, triangles, ST, and normals
		// already baked in.
		break;
	default:
		assert (false);
	}

	// fill in the ST coords at the end of the buffer for MDL and MD3:
	if (hdr->poseverttype == PV_QUAKE1)
	{
		assert (hdr->nextsurface == NULL);

		meshst_t *st = (meshst_t *)(vbodata + hdr->vbostofs);
		for (int f = 0; f < hdr->numverts_vbo; f++)
		{
			st[f].st[0] = ((float)desc[f].st[0] + 0.5f) / (float)hdr->skinwidth;
			st[f].st[1] = ((float)desc[f].st[1] + 0.5f) / (float)hdr->skinheight;
		}
	}
	else if (hdr->poseverttype == PV_QUAKE3)
	{
		meshst_t *st = (meshst_t *)(vbodata + hdr->vbostofs);
		for (int f = 0; f < hdr->numverts_vbo; f++)
		{
			// md3 has floating-point skin coords. use the values directly.
			st[f].st[0] = desc[f].st[0];
			st[f].st[1] = desc[f].st[1];
		}
	}

	/* Keep final layout bytes, rather than reparse assets or retain old GPU
	 * addresses. The MD5 completion hook replaces its temporary pose borrow. */
	alias_gpu_upload_t *upload = Mem_Alloc (sizeof (*upload));
	memset (upload, 0, sizeof (*upload));
	upload->vertices = vbodata;
	upload->vertex_size = totalvbosize;
	upload->indexes = Mem_Alloc (totalindexsize);
	upload->index_size = totalindexsize;
	memcpy (upload->indexes, indexes, totalindexsize);
	if (skeleton_indexes && num_skeleton_indexes > 0)
	{
		upload->skeleton_indexes = Mem_Alloc (skeleton_index_size);
		upload->skeleton_size = skeleton_index_size;
		memcpy (upload->skeleton_indexes, skeleton_indexes, skeleton_index_size);
	}
	upload->joints = (const byte *)joints;
	upload->joint_size = joints ? totaljointssize : 0;
	upload->joints_ready = joints == NULL;
	if (joints && hdr->avatar_static_prop)
	{
		/* Private prop upload supplies a stack-local identity pose, with
		 * no model skeleton lifetime to borrow after this call returns. */
		byte *owned = Mem_Alloc (totaljointssize);
		memcpy (owned, joints, totaljointssize);
		upload->joints = owned;
		upload->owns_joints = upload->joints_ready = true;
	}
	hdr->gpu_upload = upload;
	GLMesh_CreateBuffersFromUpload (mod, hdr);
}

/*
================
GLMesh_DeleteAllMeshBuffers

Delete VBOs for all loaded alias models
================
*/
void GLMesh_DeleteAllMeshBuffers (void)
{
	qmodel_t *m;

	/* Cosmetic and other client-local MD5 models may be loaded without a
	 * server precache slot. They still own Vulkan buffers and must participate
	 * in the same reset/retirement path as precached alias models. */
	for (int j = 0; j < mod_numknown; j++)
	{
		m = &mod_known[j];
		if (m->needload || m->type != mod_alias)
			continue;

		for (int prop = 0; prop < MD5_AVATAR_PROP_COUNT; ++prop)
			GLMesh_DeleteMeshBuffers (m->avatar_prop_gpu[prop]);

		for (int i = 0; i < PV_SIZE; ++i)
		{
			GLMesh_DeleteMeshBuffers ((aliashdr_t *)m->extradata[i]);
		}
	}
}

/* The MD5 loader calls this before releasing its temporary skinning poses.
 * All surfaces normally reuse the one already retained model pose span. */
void GLMesh_BindRetainedMD5Poses (qmodel_t *model)
{
	md5_skeleton_view_t skeleton;
	size_t pose_count = 0, pose_bytes = 0;
	const byte *poses = NULL;
	if (Mod_GetMD5Skeleton (model, &skeleton) && skeleton.absolute_poses &&
		GLMesh_CheckedSizeMul (skeleton.joint_count, skeleton.pose_count, &pose_count) &&
		GLMesh_CheckedSizeMul (pose_count, sizeof (float[12]), &pose_bytes))
		poses = (const byte *)skeleton.absolute_poses;
	for (aliashdr_t *hdr = (aliashdr_t *)model->extradata[PV_MD5]; hdr; hdr = hdr->nextsurface)
	{
		alias_gpu_upload_t *upload = hdr->gpu_upload;
		if (!upload || !upload->joints || upload->joints_ready)
			continue;
		if (poses && pose_bytes == upload->joint_size &&
			!memcmp (poses, upload->joints, pose_bytes))
			upload->joints = poses;
		else
		{
			byte *owned = Mem_Alloc (upload->joint_size);
			memcpy (owned, upload->joints, upload->joint_size);
			upload->joints = owned;
			upload->owns_joints = true;
		}
		upload->joints_ready = true;
	}
}

static qboolean GLMesh_SurfaceGPUEmpty (const aliashdr_t *hdr)
{
	return !hdr->vertex_buffer && !hdr->index_buffer && !hdr->joints_buffer &&
		!hdr->skeleton_index_buffer && !hdr->joints_set && !hdr->avatar_prop_blas;
}

static qboolean GLMesh_SurfaceCanReplay (const aliashdr_t *hdr)
{
	const alias_gpu_upload_t *upload = hdr->gpu_upload;
	size_t index_bytes, skeleton_bytes;
	if (hdr->numindexes == 0 && !upload)
		return GLMesh_SurfaceGPUEmpty (hdr);
	return hdr->numindexes > 0 && hdr->num_skeleton_indexes >= 0 &&
		GLMesh_CheckedSizeMul ((size_t)hdr->numindexes, sizeof (unsigned short), &index_bytes) &&
		GLMesh_CheckedSizeMul ((size_t)hdr->num_skeleton_indexes, sizeof (unsigned short), &skeleton_bytes) &&
		upload && upload->vertices && upload->vertex_size && upload->vertex_size <= INT_MAX &&
		upload->indexes && upload->index_size == index_bytes &&
		upload->skeleton_size == skeleton_bytes &&
		(!skeleton_bytes || upload->skeleton_indexes) && upload->joints_ready &&
		(!upload->joint_size || upload->joints);
}

typedef enum { MESH_REPLAY_CHECK, MESH_REPLAY_CHECK_EMPTY, MESH_REPLAY_RETIRE, MESH_REPLAY_CREATE } mesh_replay_operation_t;

/* Same ownership traversal as full model disposal; no precache-only subset or
 * second asset registry. The transaction caller controls GPU/task retirement. */
static qboolean GLMesh_VisitReplayModels (mesh_replay_operation_t operation)
{
	for (int j = 0; j < mod_numknown; ++j)
	{
		qmodel_t *model = &mod_known[j];
		if (model->needload || model->type != mod_alias)
			continue;
		for (int root_index = 0; root_index < PV_SIZE + MD5_AVATAR_PROP_COUNT; ++root_index)
		{
			aliashdr_t *root = root_index < PV_SIZE
				? (aliashdr_t *)model->extradata[root_index]
				: model->avatar_prop_gpu[root_index - PV_SIZE];
			if (operation == MESH_REPLAY_RETIRE)
			{
				GLMesh_DeleteMeshBuffersInternal (root, false);
				continue;
			}
			for (aliashdr_t *hdr = root; hdr; hdr = hdr->nextsurface)
			{
				if (!GLMesh_SurfaceCanReplay (hdr) ||
					(operation == MESH_REPLAY_CHECK_EMPTY && !GLMesh_SurfaceGPUEmpty (hdr)))
					return false;
				if (operation == MESH_REPLAY_CREATE && hdr->gpu_upload && GLMesh_SurfaceGPUEmpty (hdr))
					GLMesh_CreateBuffersFromUpload (model, hdr);
			}
		}
	}
	return true;
}

qboolean GLMesh_CanReplayAllMeshBuffers (void)
{
	return !isDedicated && GLMesh_VisitReplayModels (MESH_REPLAY_CHECK);
}

qboolean GLMesh_RetireAllMeshBuffers (void)
{
	if (!GLMesh_CanReplayAllMeshBuffers ())
		return false;
	return GLMesh_VisitReplayModels (MESH_REPLAY_RETIRE);
}

qboolean GLMesh_ReplayAllMeshBuffers (void)
{
	/* Validate every surface before the first allocation, including that no old
	 * GPU handle remains. Shared references created during this walk are skipped. */
	if (isDedicated || !GLMesh_VisitReplayModels (MESH_REPLAY_CHECK_EMPTY))
		return false;
	return GLMesh_VisitReplayModels (MESH_REPLAY_CREATE);
}

/*
================
R_AllocateEntityBLAS

Allocate acceleration structure for an animated entity model.
Handles MDL (PV_QUAKE1), MD3 (PV_QUAKE3), and MD5 (PV_MD5) models.
================
*/
static void R_EntityBLASSetVkGeometry (
	VkAccelerationStructureGeometryKHR *geometry, const entity_blas_surface_t *surface, VkDeviceAddress vertex_address)
{
	memset (geometry, 0, sizeof (*geometry));
	geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry->geometry.triangles.vertexData.deviceAddress = vertex_address;
	geometry->geometry.triangles.vertexStride = sizeof (float) * 3;
	geometry->geometry.triangles.maxVertex = surface->numverts_vbo - 1;
	geometry->geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;
	geometry->geometry.triangles.indexData.deviceAddress = surface->index_buffer_address;
}

static qboolean R_EntityBLASLayoutMatches (
	const entity_blas_t *blas, const qmodel_t *model, const aliashdr_t *geometry, const entity_blas_surface_t *surfaces, uint32_t surface_count)
{
	return blas && blas->model == model && blas->geometry == geometry && blas->surface_count == surface_count && blas->surfaces &&
		   memcmp (blas->surfaces, surfaces, surface_count * sizeof (*surfaces)) == 0;
}

static void R_AllocateEntityBLASInternal (entity_t *e, qboolean allow_tracked_palette)
{
	if (!vulkan_globals.ray_query || r_rtshadows.value <= 0)
		return;
	if (!e->model || e->model->type != mod_alias)
		return;

	const int modelflags = e->model->flags | ((e->effects >> 24) & 0xff);
	if (modelflags & (EF_ROCKET | EF_GRENADE | EF_TRACER | EF_TRACER2 | EF_TRACER3))
		return;

	// Efrag collection precedes this frame's palette preparation. Preserve any
	// existing BLAS until the post-preparation pass resolves its selected model.
	if (!allow_tracked_palette && e->blas_data)
		return;

	aliashdr_t *hdr = R_EntityBLASGeometry (e, allow_tracked_palette);
	if (!hdr)
	{
		R_FreeEntityBLAS (e);
		return;
	}

	entity_blas_surface_t surface_layout[MAX_SURFACES];
	uint32_t surface_count = 0;
	const r_vrik_prepared_palette_t *tracked_palette = R_EntityBLASPalette (e, hdr);
	qmodel_t *selected_model = tracked_palette ? (qmodel_t *)tracked_palette->model : e->model;
	if (!R_EntityBLASCollectSurfaces (hdr, tracked_palette, surface_layout, &surface_count))
	{
		R_FreeEntityBLAS (e);
		return;
	}

	/* Recreate storage whenever any surface/count/input identity changes. UPDATE
	 * requires the same geometry count and per-geometry primitive counts. */
	if (e->blas_data && !R_EntityBLASLayoutMatches (e->blas_data, selected_model, hdr, surface_layout, surface_count))
		R_FreeEntityBLAS (e);
	if (e->blas_data)
		return;

	// Allocate BLAS data struct (after validation to avoid alloc/free cycles)
	e->blas_data = Mem_Alloc (sizeof (entity_blas_t));
	memset (e->blas_data, 0, sizeof (entity_blas_t));
	e->blas_data->needs_initial_build = true;
	e->blas_data->surface_count = surface_count;
	e->blas_data->surfaces = Mem_Alloc (surface_count * sizeof (*e->blas_data->surfaces));
	memcpy (e->blas_data->surfaces, surface_layout, surface_count * sizeof (*surface_layout));

	// Size the one entity-owned BLAS against every chained surface.
	VkAccelerationStructureGeometryKHR blas_geometries[MAX_SURFACES];
	uint32_t primitive_counts[MAX_SURFACES];
	for (uint32_t i = 0; i < surface_count; ++i)
	{
		R_EntityBLASSetVkGeometry (&blas_geometries[i], &surface_layout[i], 0);
		primitive_counts[i] = (uint32_t)surface_layout[i].numtris;
	}

	ZEROED_STRUCT (VkAccelerationStructureBuildGeometryInfoKHR, blas_geometry_info);
	blas_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	blas_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	blas_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	blas_geometry_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	blas_geometry_info.geometryCount = surface_count;
	blas_geometry_info.pGeometries = blas_geometries;

	// Query acceleration structure size
	ZEROED_STRUCT (VkAccelerationStructureBuildSizesInfoKHR, blas_sizes_info);
	blas_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vulkan_globals.vk_get_acceleration_structure_build_sizes (
		vulkan_globals.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_geometry_info, primitive_counts, &blas_sizes_info);

	// Create buffer for BLAS
	ZEROED_STRUCT (VkBufferCreateInfo, buffer_create_info);
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.size = blas_sizes_info.accelerationStructureSize;
	buffer_create_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

	VkResult err = vkCreateBuffer (vulkan_globals.device, &buffer_create_info, NULL, &e->blas_data->buffer);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateBuffer failed for entity BLAS with code %i", (int)err);

	// Allocate from mesh heap
	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, e->blas_data->buffer, &memory_requirements);

	SDL_LockMutex (mesh_mutex);
	e->blas_data->allocation = GL_HeapAllocate (mesh_buffer_heap, memory_requirements.size, memory_requirements.alignment, &num_vulkan_mesh_allocations);
	SDL_UnlockMutex (mesh_mutex);
	err = vkBindBufferMemory (
		vulkan_globals.device, e->blas_data->buffer, GL_HeapGetAllocationMemory (e->blas_data->allocation),
		GL_HeapGetAllocationOffset (e->blas_data->allocation));
	if (err != VK_SUCCESS)
		Sys_Error ("vkBindBufferMemory failed for entity BLAS with code %i", (int)err);

	// Create acceleration structure
	ZEROED_STRUCT (VkAccelerationStructureCreateInfoKHR, acceleration_structure_create_info);
	acceleration_structure_create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	acceleration_structure_create_info.buffer = e->blas_data->buffer;
	acceleration_structure_create_info.size = blas_sizes_info.accelerationStructureSize;
	acceleration_structure_create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	err = vulkan_globals.vk_create_acceleration_structure (vulkan_globals.device, &acceleration_structure_create_info, NULL, &e->blas_data->blas);
	if (err != VK_SUCCESS)
		Sys_Error ("vkCreateAccelerationStructure failed for entity BLAS with code %i", (int)err);

	// Get device address
	ZEROED_STRUCT (VkAccelerationStructureDeviceAddressInfoKHR, address_info);
	address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	address_info.accelerationStructure = e->blas_data->blas;
	e->blas_data->address = vulkan_globals.vk_get_acceleration_structure_device_address (vulkan_globals.device, &address_info);

	// Store scratch sizes for per-frame rebuilds/updates
	e->blas_data->build_scratch_size = blas_sizes_info.buildScratchSize;
	e->blas_data->update_scratch_size = blas_sizes_info.updateScratchSize;

	// Track the model whose geometry this original entity's BLAS contains.
	e->blas_data->model = selected_model;
	e->blas_data->geometry = hdr;
}

void R_AllocateEntityBLAS (entity_t *e)
{
	/* Efrag collection runs before this frame's palette has been prepared. */
	R_AllocateEntityBLASInternal (e, false);
}

void R_AllocateEntityBLASForVRIK (entity_t *e)
{
	/* Called only after GL_PrepareVRIKRenderTask in the frame graph. */
	R_AllocateEntityBLASInternal (e, true);
}

/*
================
R_FreeEntityBLAS

Free acceleration structure for an entity
================
*/
void R_FreeEntityBLAS (entity_t *e)
{
	if (!e || !e->blas_data)
		return;

	// Add to garbage collection - resources will be freed after GPU is done with them
	if (e->blas_data->blas != VK_NULL_HANDLE)
		AddBLASGarbage (e->blas_data->blas, e->blas_data->buffer, e->blas_data->allocation);

	SAFE_FREE (e->blas_data->surfaces);
	Mem_Free (e->blas_data);
	e->blas_data = NULL;
}

/*
================
R_FreeAllEntityBLASes

Free all entity BLASes. Called when RT shadows are disabled.
================
*/
void R_FreeAllEntityBLASes (void)
{
	if (cl.entities)
	{
		for (int i = 0; i < cl.num_entities; i++)
			R_FreeEntityBLAS (&cl.entities[i]);

		for (int i = 0; i < cl.num_statics; i++)
			R_FreeEntityBLAS (cl.static_entities[i]);
	}

	for (int model = 0; model < mod_numknown; ++model)
		for (int prop = 0; prop < MD5_AVATAR_PROP_COUNT; ++prop)
			GLMesh_FreeAvatarPropBLAS (mod_known[model].avatar_prop_gpu[prop]);
}

/* The prop's uploaded MD5 vertices already contain their rigid, bone-local
 * xyz. Unlike animated entities, no compute skinning or per-player BLAS is
 * needed: the TLAS instance supplies each player's attachment transform. */
static qboolean GLMesh_AvatarPropGeometries (
	const aliashdr_t *root, VkAccelerationStructureGeometryKHR geometries[MAX_SURFACES],
	VkAccelerationStructureBuildRangeInfoKHR ranges[MAX_SURFACES], uint32_t primitives[MAX_SURFACES], uint32_t *count_out)
{
	if (!root || !root->avatar_static_prop || !geometries || !ranges || !primitives || !count_out)
		return false;

	const VkPhysicalDeviceAccelerationStructurePropertiesKHR *limits = &vulkan_globals.physical_device_acceleration_structure_properties;
	uint32_t count = 0;
	uint64_t total_primitives = 0;
	for (const aliashdr_t *surface = root; surface; surface = surface->nextsurface)
	{
		if (count >= MAX_SURFACES || count >= limits->maxGeometryCount || !surface->avatar_static_prop || surface->poseverttype != PV_MD5 ||
			surface->numjoints != 1 || surface->numframes != 1 || surface->numposes != 1 || surface->numverts_vbo <= 0 ||
			surface->numverts_vbo > (int)UINT16_MAX + 1 || surface->numtris <= 0 || surface->numindexes != (int64_t)surface->numtris * 3 ||
			(uint64_t)surface->numtris > limits->maxPrimitiveCount - total_primitives ||
			surface->vertex_buffer == VK_NULL_HANDLE || surface->index_buffer == VK_NULL_HANDLE ||
			!surface->vertex_buffer_address || !surface->index_buffer_address ||
			(surface->vertex_buffer_address % sizeof (float)) || (surface->index_buffer_address % sizeof (uint16_t)))
			return false;
		total_primitives += (uint64_t)surface->numtris;

		VkAccelerationStructureGeometryKHR *geometry = &geometries[count];
		memset (geometry, 0, sizeof (*geometry));
		geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
		geometry->geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		geometry->geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		geometry->geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		geometry->geometry.triangles.vertexData.deviceAddress = surface->vertex_buffer_address + offsetof (md5vert_t, xyz);
		geometry->geometry.triangles.vertexStride = sizeof (md5vert_t);
		geometry->geometry.triangles.maxVertex = (uint32_t)surface->numverts_vbo - 1;
		geometry->geometry.triangles.indexType = VK_INDEX_TYPE_UINT16;
		geometry->geometry.triangles.indexData.deviceAddress = surface->index_buffer_address;
		geometry->flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

		memset (&ranges[count], 0, sizeof (ranges[count]));
		ranges[count].primitiveCount = primitives[count] = (uint32_t)surface->numtris;
		++count;
	}
	*count_out = count;
	return count != 0;
}

qboolean GLMesh_AvatarPropBLASReady (const aliashdr_t *root)
{
	if (!vulkan_globals.ray_query || r_rtshadows.value <= 0 || !root || !root->avatar_static_prop ||
		!root->avatar_prop_blas_built || root->avatar_prop_blas == VK_NULL_HANDLE ||
		root->avatar_prop_blas_buffer == VK_NULL_HANDLE || !root->avatar_prop_blas_allocation || !root->avatar_prop_blas_address)
		return false;
	for (const aliashdr_t *surface = root; surface; surface = surface->nextsurface)
		if (surface->vertex_buffer == VK_NULL_HANDLE || surface->index_buffer == VK_NULL_HANDLE ||
			!surface->vertex_buffer_address || !surface->index_buffer_address)
			return false;
	return true;
}

VkDeviceAddress GLMesh_AvatarPropBLASAddress (const aliashdr_t *root)
{
	return GLMesh_AvatarPropBLASReady (root) ? root->avatar_prop_blas_address : 0;
}

static void GLMesh_DiscardUnbuiltAvatarPropBLAS (
	VkAccelerationStructureKHR blas, VkBuffer buffer, glheapallocation_t *allocation)
{
	if (blas != VK_NULL_HANDLE)
		vulkan_globals.vk_destroy_acceleration_structure (vulkan_globals.device, blas, NULL);
	if (buffer != VK_NULL_HANDLE)
		vkDestroyBuffer (vulkan_globals.device, buffer, NULL);
	if (allocation)
	{
		SDL_LockMutex (mesh_mutex);
		GL_HeapFree (mesh_buffer_heap, allocation, &num_vulkan_mesh_allocations);
		SDL_UnlockMutex (mesh_mutex);
	}
}

static void GLMesh_BuildAvatarPropBLAS (cb_context_t *cbx, aliashdr_t *root)
{
	VkAccelerationStructureGeometryKHR geometries[MAX_SURFACES];
	VkAccelerationStructureBuildRangeInfoKHR ranges[MAX_SURFACES];
	uint32_t primitives[MAX_SURFACES];
	uint32_t count = 0;
	if (!GLMesh_AvatarPropGeometries (root, geometries, ranges, primitives, &count))
	{
		root->avatar_prop_blas_failed = true;
		return;
	}

	ZEROED_STRUCT (VkAccelerationStructureBuildGeometryInfoKHR, build);
	build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = count;
	build.pGeometries = geometries;
	ZEROED_STRUCT (VkAccelerationStructureBuildSizesInfoKHR, sizes);
	sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	vulkan_globals.vk_get_acceleration_structure_build_sizes (
		vulkan_globals.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, primitives, &sizes);

	const VkDeviceSize alignment = q_max (
		(VkDeviceSize)vulkan_globals.physical_device_acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment,
		(VkDeviceSize)1);
	/* R_EnsureASScratchBufferSize rounds up in uint32_t. Leave room for an
	 * address-alignment adjustment even if the buffer base is not aligned. */
	const VkDeviceSize scratch_limit = ((VkDeviceSize)UINT32_MAX + 1) / 2;
	if (!sizes.accelerationStructureSize || !sizes.buildScratchSize ||
		alignment > scratch_limit || sizes.buildScratchSize > scratch_limit - (alignment - 1))
	{
		root->avatar_prop_blas_failed = true;
		return;
	}
	R_EnsureASScratchBufferSize ((uint32_t)(sizes.buildScratchSize + alignment - 1));
	const VkDeviceAddress base = as_scratch_buffer.device_address;
	const VkDeviceSize adjust = (alignment - (base % alignment)) % alignment;
	if (as_scratch_buffer.buffer == VK_NULL_HANDLE || !base || base > UINT64_MAX - adjust ||
		adjust > as_scratch_buffer_size || sizes.buildScratchSize > as_scratch_buffer_size - adjust)
	{
		root->avatar_prop_blas_failed = true;
		return;
	}

	VkBuffer buffer = VK_NULL_HANDLE;
	glheapallocation_t *allocation = NULL;
	VkAccelerationStructureKHR blas = VK_NULL_HANDLE;
	ZEROED_STRUCT (VkBufferCreateInfo, buffer_info);
	buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_info.size = sizes.accelerationStructureSize;
	buffer_info.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	VkResult err = vkCreateBuffer (vulkan_globals.device, &buffer_info, NULL, &buffer);
	if (err != VK_SUCCESS)
	{
		buffer = VK_NULL_HANDLE;
		goto failed;
	}
	GL_SetObjectName ((uint64_t)buffer, VK_OBJECT_TYPE_BUFFER, "Avatar prop BLAS");
	VkMemoryRequirements requirements;
	vkGetBufferMemoryRequirements (vulkan_globals.device, buffer, &requirements);
	SDL_LockMutex (mesh_mutex);
	allocation = GL_HeapAllocate (mesh_buffer_heap, requirements.size, requirements.alignment, &num_vulkan_mesh_allocations);
	SDL_UnlockMutex (mesh_mutex);
	err = vkBindBufferMemory (
		vulkan_globals.device, buffer, GL_HeapGetAllocationMemory (allocation), GL_HeapGetAllocationOffset (allocation));
	if (err != VK_SUCCESS)
		goto failed;
	ZEROED_STRUCT (VkAccelerationStructureCreateInfoKHR, create_info);
	create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	create_info.buffer = buffer;
	create_info.size = sizes.accelerationStructureSize;
	create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	err = vulkan_globals.vk_create_acceleration_structure (vulkan_globals.device, &create_info, NULL, &blas);
	if (err != VK_SUCCESS)
	{
		blas = VK_NULL_HANDLE;
		goto failed;
	}
	ZEROED_STRUCT (VkAccelerationStructureDeviceAddressInfoKHR, address_info);
	address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	address_info.accelerationStructure = blas;
	const VkDeviceAddress address = vulkan_globals.vk_get_acceleration_structure_device_address (vulkan_globals.device, &address_info);
	if (!address)
	{
		err = VK_ERROR_INITIALIZATION_FAILED;
		goto failed;
	}

	build.dstAccelerationStructure = blas;
	build.scratchData.deviceAddress = base + adjust;
	/* Staging copies are submitted before the frame AS command buffer on the
	 * same queue. The geometry reads use SHADER_READ at AS-build stage. */
	ZEROED_STRUCT (VkMemoryBarrier, input_barrier);
	input_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	input_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	input_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vulkan_globals.vk_cmd_pipeline_barrier (
		cbx->cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0,
		1, &input_barrier, 0, NULL, 0, NULL);
	const VkAccelerationStructureBuildRangeInfoKHR *ranges_ptr = ranges;
	vulkan_globals.vk_cmd_build_acceleration_structures (cbx->cb, 1, &build, &ranges_ptr);
	/* The next prop or animated body can reuse scratch; TLAS can read this BLAS.
	 * Scratch uses AS_READ|AS_WRITE, while compute may overwrite it next. */
	ZEROED_STRUCT (VkMemoryBarrier, build_barrier);
	build_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	build_barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	build_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR |
		VK_ACCESS_SHADER_WRITE_BIT;
	vulkan_globals.vk_cmd_pipeline_barrier (
		cbx->cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
		1, &build_barrier, 0, NULL, 0, NULL);

	root->avatar_prop_blas = blas;
	root->avatar_prop_blas_buffer = buffer;
	root->avatar_prop_blas_allocation = allocation;
	root->avatar_prop_blas_address = address;
	root->avatar_prop_blas_built = true;
	return;

failed:
	Con_Warning ("Avatar prop BLAS allocation failed (%d); keeping Ranger fallback\n", (int)err);
	GLMesh_DiscardUnbuiltAvatarPropBLAS (blas, buffer, allocation);
	root->avatar_prop_blas_failed = true;
}

void GLMesh_BuildPendingAvatarPropBLASes (cb_context_t *cbx)
{
	if (!cbx || cbx->cb == VK_NULL_HANDLE || !vulkan_globals.ray_query || r_rtshadows.value <= 0 || !mesh_buffer_heap)
		return;
	for (int model = 0; model < mod_numknown; ++model)
	{
		qmodel_t *mod = &mod_known[model];
		if (mod->needload || mod->type != mod_alias)
			continue;
		for (int prop = 0; prop < MD5_AVATAR_PROP_COUNT; ++prop)
		{
			aliashdr_t *root = mod->avatar_prop_gpu[prop];
			if (root && root->avatar_static_prop && !root->avatar_prop_blas_built && !root->avatar_prop_blas_failed)
				GLMesh_BuildAvatarPropBLAS (cbx, root);
		}
	}
}

/*
================
R_UpdateAnimatedBLASes

Update all entity BLASes with animated vertex data.
This dispatches compute shaders to interpolate/skin vertices into the
scratch buffer, then builds/updates the BLASes.
================
*/
#define MAX_PENDING_BLAS_BUILDS 256

static VkAccelerationStructureGeometryKHR			   pending_geometries[MAX_PENDING_BLAS_BUILDS][MAX_SURFACES];
static VkAccelerationStructureBuildGeometryInfoKHR	   pending_build_infos[MAX_PENDING_BLAS_BUILDS];
static VkAccelerationStructureBuildRangeInfoKHR		   pending_range_infos[MAX_PENDING_BLAS_BUILDS][MAX_SURFACES];
static const VkAccelerationStructureBuildRangeInfoKHR *pending_range_info_ptrs[MAX_PENDING_BLAS_BUILDS];

typedef struct
{
	entity_t		 *entity;
	entity_blas_t	 *blas_data;
	int			  pose1;
	int			  pose2;
	float			  blend;
	qboolean		  cacheable;
} pending_blas_pose_t;

static pending_blas_pose_t pending_blas_poses[MAX_PENDING_BLAS_BUILDS];

static qboolean R_EntityBLASPoseCacheMatches (
	const entity_t *e, const aliashdr_t *hdr, int pose1, int pose2, float blend, const r_vrik_prepared_palette_t *tracked_palette)
{
	const entity_blas_t *blas = e ? e->blas_data : NULL;
	const qmodel_t *selected_model = tracked_palette ? tracked_palette->model : (e ? e->model : NULL);
	if (!blas || !hdr || tracked_palette || !isfinite (blend) || blas->needs_initial_build || !blas->pose_cache_valid || blas->model != selected_model ||
		blas->geometry != hdr)
		return false;

	return blas->cached_model == selected_model && blas->cached_geometry == hdr && blas->cached_pose1 == pose1 && blas->cached_pose2 == pose2 &&
		   memcmp (&blas->cached_blend, &blend, sizeof (blend)) == 0;
}

static qboolean R_EntityBLASComputeScratchLayout (
	const entity_blas_t *blas, VkDeviceAddress base_address, VkDeviceSize start_offset, VkDeviceSize buffer_alignment, VkDeviceSize scratch_alignment, VkDeviceSize as_scratch_size,
	VkDeviceSize *vertex_offsets, VkDeviceSize *as_scratch_offset, VkDeviceSize *end_offset)
{
	if (!blas || !blas->surfaces || !blas->surface_count || !buffer_alignment || !scratch_alignment || !as_scratch_offset || !end_offset)
		return false;

	VkDeviceSize cursor = start_offset;
	for (uint32_t i = 0; i < blas->surface_count; ++i)
	{
		const entity_blas_surface_t *surface = &blas->surfaces[i];
		if (surface->numverts_vbo <= 0 || cursor > UINT64_MAX - base_address || base_address + cursor > UINT64_MAX - (buffer_alignment - 1))
			return false;
		const VkDeviceSize aligned_address = q_align (base_address + cursor, buffer_alignment);
		cursor = aligned_address - base_address;
		if (vertex_offsets)
			vertex_offsets[i] = cursor;
		const VkDeviceSize vertex_size = (VkDeviceSize)surface->numverts_vbo * sizeof (float) * 3;
		if (vertex_size > UINT64_MAX - cursor)
			return false;
		cursor += vertex_size;
	}

	if (cursor > UINT64_MAX - base_address || base_address + cursor > UINT64_MAX - (scratch_alignment - 1))
		return false;
	*as_scratch_offset = q_align (base_address + cursor, scratch_alignment) - base_address;
	if (as_scratch_size > UINT64_MAX - *as_scratch_offset)
		return false;
	*end_offset = *as_scratch_offset + as_scratch_size;
	return true;
}

static qboolean R_EntityBLASPosesFit (const entity_blas_t *blas, int pose1, int pose2)
{
	if (!blas || !blas->surfaces || pose1 < 0 || pose2 < 0)
		return false;
	for (uint32_t i = 0; i < blas->surface_count; ++i)
	{
		const entity_blas_surface_t *surface = &blas->surfaces[i];
		const int pose_count = (surface->poseverttype == PV_QUAKE1) ? surface->numposes : surface->numframes;
		const uint32_t elements_per_pose = (surface->poseverttype == PV_MD5 || surface->poseverttype == PV_MD5_8) ?
			(uint32_t)surface->numjoints : (uint32_t)surface->numverts_vbo;
		if (pose1 >= pose_count || pose2 >= pose_count || !elements_per_pose ||
			(uint64_t)pose1 * elements_per_pose > UINT32_MAX || (uint64_t)pose2 * elements_per_pose > UINT32_MAX)
			return false;
	}
	return true;
}

/*
================
R_FlushPendingBLASBuilds

Flushes pending BLAS builds: inserts compute->AS barrier, builds all pending AS, then inserts appropriate barrier for next phase.
================
*/
static void R_FlushPendingBLASBuilds (cb_context_t *cbx, int num_pending, qboolean more_entities)
{
	if (num_pending == 0)
		return;

	// Geometry inputs use SHADER_READ at the AS build stage; AS_READ is for
	// acceleration structures and scratch, not the interpolated vertices.
	ZEROED_STRUCT (VkMemoryBarrier, compute_barrier);
	compute_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	compute_barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	compute_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vulkan_globals.vk_cmd_pipeline_barrier (
		cbx->cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &compute_barrier, 0, NULL, 0, NULL);

	// Single batched AS build call
	vulkan_globals.vk_cmd_build_acceleration_structures (cbx->cb, num_pending, pending_build_infos, pending_range_info_ptrs);

	// Cache only after both the compute dispatches and BLAS builds are recorded.
	// A tracked palette or invalid blend must invalidate any earlier local pose.
	for (int i = 0; i < num_pending; ++i)
	{
		pending_blas_pose_t *pending = &pending_blas_poses[i];
		entity_blas_t	   *blas = pending->blas_data;
		if (!pending->entity || pending->entity->blas_data != blas)
			continue;

		blas->needs_initial_build = false;
		blas->pose_cache_valid = false;
		if (!pending->cacheable)
			continue;

		blas->cached_model = blas->model;
		blas->cached_geometry = blas->geometry;
		blas->cached_pose1 = pending->pose1;
		blas->cached_pose2 = pending->pose2;
		memcpy (&blas->cached_blend, &pending->blend, sizeof (blas->cached_blend));
		blas->pose_cache_valid = true;
	}

	// Barrier for next phase
	ZEROED_STRUCT (VkMemoryBarrier, as_barrier);
	as_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	as_barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	as_barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;

	if (more_entities)
	{
		// Reuse scratch memory for the next compute and BLAS build batch.
		as_barrier.dstAccessMask |= VK_ACCESS_SHADER_WRITE_BIT;
		vulkan_globals.vk_cmd_pipeline_barrier (
			cbx->cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &as_barrier, 0, NULL, 0, NULL);
	}
	else
	{
		// TLAS reads the BLASes and reuses their scratch memory.
		vulkan_globals.vk_cmd_pipeline_barrier (
			cbx->cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &as_barrier, 0, NULL,
			0, NULL);
	}
}

void R_UpdateAnimatedBLASes (cb_context_t *cbx)
{
	if (!vulkan_globals.ray_query)
		return;
	if (as_scratch_buffer.buffer == VK_NULL_HANDLE)
		return;

	const VkDeviceSize scratch_alignment = q_max (
	(VkDeviceSize)vulkan_globals.physical_device_acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment, (VkDeviceSize)1);
	// 16 bytes because of device address default buffer_reference_align
	const VkDeviceSize buffer_alignment = q_max ((VkDeviceSize)vulkan_globals.device_properties.limits.minStorageBufferOffsetAlignment, (VkDeviceSize)16);
	const int		   total_entities = cl.num_entities + cl.num_statics;

	// Pre-pass: find max scratch size needed across all entities and resize if necessary
	{
		VkDeviceSize max_scratch_needed = 0;
		for (int i = 0; i < total_entities; ++i)
		{
			entity_t *e = (i < cl.num_entities) ? &cl.entities[i] : cl.static_entities[i - cl.num_entities];
			if (!e->model || e->model->needload || e->model->type != mod_alias)
				continue;
			if ((e->alpha != ENTALPHA_DEFAULT) && (ENTALPHA_DECODE (e->alpha) < 1.0f))
				continue;
			const r_vrik_prepared_palette_t *prepared =
				(i > 0 && i <= cl.maxclients) ? R_VRIKRenderLookup (e) : NULL;
			if (e->blas_data || prepared)
				R_AllocateEntityBLASForVRIK (e);
			if (!e->blas_data || e->blas_data->blas == VK_NULL_HANDLE)
				continue;
			aliashdr_t *hdr = (aliashdr_t *)e->blas_data->geometry;
			if (!hdr || !e->blas_data->surface_count)
				continue;
			const r_vrik_prepared_palette_t *tracked_palette = R_EntityBLASPalette (e, hdr);
			const qmodel_t *selected_model = tracked_palette ? tracked_palette->model : e->model;
			if (e->blas_data->model != selected_model)
				continue;
			if (!tracked_palette)
			{
				lerpdata_t lerpdata;
				R_SetupAliasFrame (e, hdr, &lerpdata);
				if (R_EntityBLASPoseCacheMatches (e, hdr, lerpdata.pose1, lerpdata.pose2, lerpdata.blend, NULL))
					continue;
			}

			const VkDeviceSize as_scratch_size = e->blas_data->needs_initial_build ? e->blas_data->build_scratch_size : e->blas_data->update_scratch_size;
			VkDeviceSize vertex_bytes = 0;
			qboolean layout_fits = true;
			for (uint32_t surface = 0; surface < e->blas_data->surface_count; ++surface)
			{
				const VkDeviceSize vertex_size = (VkDeviceSize)e->blas_data->surfaces[surface].numverts_vbo * sizeof (float) * 3;
				if (vertex_size > UINT64_MAX - vertex_bytes)
				{
					layout_fits = false;
					break;
				}
				vertex_bytes += vertex_size;
			}
			const VkDeviceSize per_surface_padding = buffer_alignment - 1;
			if (e->blas_data->surface_count > (UINT64_MAX - vertex_bytes) / per_surface_padding)
				layout_fits = false;
			VkDeviceSize total_needed = 0;
			if (layout_fits)
			{
				const VkDeviceSize vertex_padding = (VkDeviceSize)e->blas_data->surface_count * per_surface_padding;
				if (vertex_padding > UINT64_MAX - vertex_bytes || scratch_alignment - 1 > UINT64_MAX - vertex_bytes - vertex_padding)
					layout_fits = false;
				else
				{
					total_needed = vertex_bytes + vertex_padding + scratch_alignment - 1;
					if (as_scratch_size > UINT64_MAX - total_needed)
						layout_fits = false;
					else
						total_needed += as_scratch_size;
				}
			}
			if (!layout_fits)
				continue;
			max_scratch_needed = q_max (max_scratch_needed, total_needed);
		}

		if (max_scratch_needed > (((VkDeviceSize)UINT32_MAX + 1) / 2))
		{
			/* R_EnsureASScratchBufferSize rounds to a uint32 power of two. */
			for (int i = 0; i < total_entities; ++i)
			{
				entity_t *e = (i < cl.num_entities) ? &cl.entities[i] : cl.static_entities[i - cl.num_entities];
				if (e->blas_data)
				{
					e->blas_data->needs_initial_build = true;
					e->blas_data->pose_cache_valid = false;
				}
			}
			return;
		}
		R_EnsureASScratchBufferSize ((uint32_t)max_scratch_needed);
	}

	const VkDeviceSize scratch_buffer_size = as_scratch_buffer_size;
	VkDeviceSize	   scratch_offset = 0;
	int				   num_pending = 0;
	int				   entity_index = 0;

	R_BeginDebugUtilsLabel (cbx, "Update Animated BLAS");

	while (entity_index < total_entities)
	{
		// Phase 1: Compute - dispatch shaders and collect build info
		while (entity_index < total_entities && num_pending < MAX_PENDING_BLAS_BUILDS)
		{
			entity_t *e = (entity_index < cl.num_entities) ? &cl.entities[entity_index] : cl.static_entities[entity_index - cl.num_entities];
			++entity_index;

			if (!e->model || e->model->needload || e->model->type != mod_alias || !e->blas_data || e->blas_data->blas == VK_NULL_HANDLE)
				continue;

			// Skip transparent entities (same as TLAS)
			if ((e->alpha != ENTALPHA_DEFAULT) && (ENTALPHA_DECODE (e->alpha) < 1.0f))
				continue;

			aliashdr_t *hdr = (aliashdr_t *)e->blas_data->geometry;
			if (!hdr || hdr->numverts_vbo == 0)
				continue;

			// Tracked joints already contain the selected avatar's complete pose.
			const r_vrik_prepared_palette_t *tracked_palette = R_EntityBLASPalette (e, hdr);
			const qmodel_t *selected_model = tracked_palette ? tracked_palette->model : e->model;
			if (e->blas_data->model != selected_model)
				continue;
			int pose1 = 0, pose2 = 0;
			float blend = 0.0f;
			if (!tracked_palette)
			{
				lerpdata_t lerpdata;
				R_SetupAliasFrame (e, hdr, &lerpdata);
				pose1 = lerpdata.pose1;
				pose2 = lerpdata.pose2;
				blend = lerpdata.blend;
			}
			if (!R_EntityBLASPosesFit (e->blas_data, pose1, pose2))
			{
				/* A changed/invalid animation layout cannot safely refit this BLAS. */
				e->blas_data->needs_initial_build = true;
				e->blas_data->pose_cache_valid = false;
				continue;
			}
			if (R_EntityBLASPoseCacheMatches (e, hdr, pose1, pose2, blend, tracked_palette))
			{
				if (scr_speeds.value == 2)
					Atomic_AddUInt32 (&rs_blas_pose_reuses, 1u);
				continue;
			}

			// Always use refit after first build. We trace few rays and full updates are expensive.
			qboolean use_update = !e->blas_data->needs_initial_build;
			const VkDeviceSize as_scratch_size = use_update ? e->blas_data->update_scratch_size : e->blas_data->build_scratch_size;
			VkDeviceSize vertex_offsets[MAX_SURFACES];
			VkDeviceSize as_scratch_offset, end_offset;
			if (!R_EntityBLASComputeScratchLayout (
				e->blas_data, as_scratch_buffer.device_address, scratch_offset, buffer_alignment, scratch_alignment, as_scratch_size, vertex_offsets,
				&as_scratch_offset, &end_offset))
			{
				e->blas_data->needs_initial_build = true;
				e->blas_data->pose_cache_valid = false;
				continue;
			}

			// Check if we have space; if not, flush current batch and reset
			if (scratch_offset > scratch_buffer_size || end_offset > scratch_buffer_size)
			{
				if (num_pending == 0)
				{
					/* The conservative pre-pass should make this unreachable; do not
					 * retry forever if a device/address limit still makes it impossible. */
					e->blas_data->needs_initial_build = true;
					e->blas_data->pose_cache_valid = false;
					continue;
				}
				// Need to flush - back up entity_index to retry this entity after flush
				--entity_index;
				break;
			}

			VkAccelerationStructureGeometryKHR *geometries = pending_geometries[num_pending];
			VkAccelerationStructureBuildRangeInfoKHR *ranges = pending_range_infos[num_pending];
			for (uint32_t surface_index = 0; surface_index < e->blas_data->surface_count; ++surface_index)
			{
				const entity_blas_surface_t *surface = &e->blas_data->surfaces[surface_index];
				const VkDeviceAddress vertex_output_address = as_scratch_buffer.device_address + vertex_offsets[surface_index];

				/* Each chained mesh owns vertex/index/joint buffers, but compatible
				 * MD5 surfaces all consume the same tracked palette. */
				if (surface->poseverttype == PV_MD5 || surface->poseverttype == PV_MD5_8)
				{
					skinning_push_constants_t pc = {
						.input_address = surface->vertex_buffer_address,
						.joints_address = tracked_palette ? tracked_palette->palette_address : surface->joints_buffer_address,
						.output_address = vertex_output_address,
						.joints_offset0 = tracked_palette ? 0 : (uint32_t)((uint64_t)pose1 * surface->numjoints),
						.joints_offset1 = tracked_palette ? 0 : (uint32_t)((uint64_t)pose2 * surface->numjoints),
						.output_offset = 0,
						.num_verts = surface->numverts_vbo,
						.blend_factor = blend,
					};
					R_BindPipeline (
						cbx, VK_PIPELINE_BIND_POINT_COMPUTE,
						(surface->poseverttype == PV_MD5_8) ? vulkan_globals.skinning_8_pipeline : vulkan_globals.skinning_pipeline);
					R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (pc), &pc);
				}
				else
				{
					mesh_interpolate_push_constants_t pc = {
						.input_address = surface->vertex_buffer_address,
						.output_address = vertex_output_address,
						.pose1_offset = (uint32_t)((uint64_t)pose1 * surface->numverts_vbo),
						.pose2_offset = (uint32_t)((uint64_t)pose2 * surface->numverts_vbo),
						.output_offset = 0,
						.num_verts = surface->numverts_vbo,
						.blend_factor = blend,
						.flags = (surface->poseverttype == PV_QUAKE3) ? 0x4 : 0,
					};
					R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan_globals.mesh_interpolate_pipeline);
					R_PushConstants (cbx, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof (pc), &pc);
				}

				const uint32_t num_groups = (uint32_t)(((uint64_t)surface->numverts_vbo + 63) / 64);
				vulkan_globals.vk_cmd_dispatch (cbx->cb, num_groups, 1, 1);
				R_EntityBLASSetVkGeometry (&geometries[surface_index], surface, vertex_output_address);
				memset (&ranges[surface_index], 0, sizeof (ranges[surface_index]));
				ranges[surface_index].primitiveCount = (uint32_t)surface->numtris;
			}

			VkAccelerationStructureBuildGeometryInfoKHR *build = &pending_build_infos[num_pending];
			memset (build, 0, sizeof (*build));
			build->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
			build->type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
			build->flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
			if (use_update)
			{
				build->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
				build->srcAccelerationStructure = e->blas_data->blas; // Required for UPDATE mode
			}
			else
			{
				build->mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
			}
			build->dstAccelerationStructure = e->blas_data->blas;
			build->geometryCount = e->blas_data->surface_count;
			build->pGeometries = geometries;
			build->scratchData.deviceAddress = as_scratch_buffer.device_address + as_scratch_offset;
			pending_range_info_ptrs[num_pending] = ranges;
			pending_blas_poses[num_pending] = (pending_blas_pose_t) {
				.entity = e,
				.blas_data = e->blas_data,
				.pose1 = pose1,
				.pose2 = pose2,
				.blend = blend,
				.cacheable = !tracked_palette && isfinite (blend),
			};
			if (scr_speeds.value == 2)
				Atomic_AddUInt32 (use_update ? &rs_blas_refits : &rs_blas_builds, 1u);

			++num_pending;

			scratch_offset = end_offset;
		}

		// Phase 2: Build - flush pending builds
		if (num_pending > 0)
		{
			qboolean more_entities = (entity_index < total_entities);
			R_FlushPendingBLASBuilds (cbx, num_pending, more_entities);
			num_pending = 0;
			scratch_offset = 0;
		}
	}

	R_EndDebugUtilsLabel (cbx);
}
