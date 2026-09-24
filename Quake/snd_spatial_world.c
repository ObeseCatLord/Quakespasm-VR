/* Static BSP geometry adapter for the optional Steam Audio room. */
#include "quakedef.h"
#include "snd_spatial_world.h"

#ifdef USE_STEAMAUDIO

#include "snd_spatial.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define SPATIAL_MAX_ROOM_VERTICES  4000000
#define SPATIAL_MAX_ROOM_TRIANGLES 4000000

static qboolean SpatialWorld_TextureContains (const texture_t *texture, const char *needle)
{
	const char *name = texture->name;
	size_t name_length = sizeof (texture->name);
	size_t needle_length = strlen (needle);
	size_t i, j;

	for (i = 0; i < name_length && name[i]; ++i)
	{
		for (j = 0; j < needle_length && i + j < name_length && name[i + j]; ++j)
		{
			if (tolower ((unsigned char)name[i + j]) !=
				tolower ((unsigned char)needle[j]))
				break;
		}
		if (j == needle_length)
			return true;
	}
	return false;
}

/* Returns false for an invalid texture reference. */
static qboolean SpatialWorld_IncludeSurface (const msurface_t *surface, qboolean *include)
{
	const unsigned int excluded_flags = SURF_DRAWSKY | SURF_DRAWTURB |
		SURF_DRAWFENCE | SURF_DRAWLAVA | SURF_DRAWSLIME | SURF_DRAWTELE |
		SURF_DRAWWATER;
	const texture_t *texture;

	*include = false;
	if (surface->flags & excluded_flags)
		return true;
	if (!surface->texinfo || !surface->texinfo->texture)
		return false;
	texture = surface->texinfo->texture;
	if (texture->type < TEXTYPE_DEFAULT || texture->type > TEXTYPE_WATER)
		return false;
	if (texture->type == TEXTYPE_SKY || texture->type == TEXTYPE_CUTOUT ||
		TEXTYPE_ISLIQUID (texture->type))
		return true;
	*include = true;
	return true;
}

static qboolean SpatialWorld_SurfaceVertex (const qmodel_t *model, int surfedge_index, int *vertex)
{
	int edge = model->surfedges[surfedge_index];
	unsigned int edge_index, vertex_index;

	if (edge == INT_MIN)
		return false;
	edge_index = (unsigned int)(edge < 0 ? -edge : edge);
	if (edge_index >= (unsigned int)model->numedges)
		return false;
	vertex_index = model->edges[edge_index].v[edge < 0 ? 1 : 0];
	if (vertex_index >= (unsigned int)model->numvertexes)
		return false;
	*vertex = (int)vertex_index;
	return true;
}

static qboolean SpatialWorld_ValidModel (const qmodel_t *model)
{
	/* cl.worldmodel is client-loaded; is_worldmodel is server-name-derived. */
	if (!model || model->numvertexes <= 0 ||
		model->numvertexes > SPATIAL_MAX_ROOM_VERTICES ||
		model->numsurfaces < 0 || model->nummodelsurfaces < 0 ||
		model->firstmodelsurface < 0 ||
		model->firstmodelsurface > model->numsurfaces ||
		model->nummodelsurfaces > model->numsurfaces - model->firstmodelsurface ||
		model->numedges < 0 || model->numsurfedges < 0 || !model->vertexes ||
		(model->nummodelsurfaces && !model->surfaces))
		return false;
	return true;
}

static qboolean SpatialWorld_AddCapacity (size_t *capacity, int numedges)
{
	size_t fan_triangles;

	if (numedges < 0)
		return false;
	if (numedges < 3)
		return true;
	fan_triangles = (size_t)numedges - 2;
	if (fan_triangles > SPATIAL_MAX_ROOM_TRIANGLES - *capacity)
		return false;
	*capacity += fan_triangles;
	return true;
}

static qboolean SpatialWorld_ValidFaceEdges (const qmodel_t *model, const msurface_t *face)
{
	if (face->numedges < 0)
		return false;
	if (face->numedges < 3)
		return true;
	return model->surfedges && model->edges && model->numedges > 0 &&
		face->firstedge >= 0 && face->firstedge <= model->numsurfedges &&
		face->numedges <= model->numsurfedges - face->firstedge;
}

static qboolean SpatialWorld_ValidPlane (const mplane_t *plane)
{
	double length_squared = 0.0;
	int axis;

	if (!plane)
		return false;
	for (axis = 0; axis < 3; ++axis)
	{
		if (!isfinite (plane->normal[axis]))
			return false;
		length_squared += (double)plane->normal[axis] * plane->normal[axis];
	}
	return length_squared > 1.0e-12;
}

static qboolean SpatialWorld_AllocGeometry (sa_geometry_t *geometry, size_t triangle_capacity)
{
	size_t vertex_count = (size_t)geometry->num_vertices;

	if (vertex_count > SIZE_MAX / (3 * sizeof (*geometry->vertices)) ||
		triangle_capacity > SIZE_MAX / (3 * sizeof (*geometry->triangles)) ||
		triangle_capacity > SIZE_MAX / sizeof (*geometry->materials))
		return false;
	geometry->vertices = (float *)malloc (vertex_count * 3 * sizeof (*geometry->vertices));
	geometry->triangles = (int *)malloc (triangle_capacity * 3 * sizeof (*geometry->triangles));
	geometry->materials = (int *)malloc (triangle_capacity * sizeof (*geometry->materials));
	return geometry->vertices && geometry->triangles && geometry->materials;
}

static int SpatialWorld_Material (const texture_t *texture)
{
	if (SpatialWorld_TextureContains (texture, "metal"))
		return 1;
	if (SpatialWorld_TextureContains (texture, "wood"))
		return 2;
	if (SpatialWorld_TextureContains (texture, "rock"))
		return 3;
	return 0;
}

void SpatialWorld_Clear (void)
{
	/* Spatial_ClearWorld detaches under callback exclusion and joins afterward. */
	Spatial_ClearWorld ();
}

void SpatialWorld_NewMap (void)
{
	qmodel_t *model = cl.worldmodel;
	sa_geometry_t geometry = {0};
	size_t triangle_capacity = 0;
	int i, j;

	if (!Spatial_Active ())
		return;
	if (!SpatialWorld_ValidModel (model))
		goto invalid_geometry;

	for (i = 0; i < model->nummodelsurfaces; ++i)
	{
		msurface_t *face = &model->surfaces[model->firstmodelsurface + i];
		qboolean include;

		if (!SpatialWorld_IncludeSurface (face, &include) ||
			!SpatialWorld_ValidFaceEdges (model, face))
			goto invalid_geometry;
		if (include && !SpatialWorld_AddCapacity (&triangle_capacity, face->numedges))
			goto oversized_geometry;
	}
	if (!triangle_capacity)
		goto invalid_geometry;

	geometry.num_vertices = model->numvertexes;
	if (!SpatialWorld_AllocGeometry (&geometry, triangle_capacity))
		goto allocation_failed;
	for (i = 0; i < geometry.num_vertices; ++i)
	{
		const float *vertex = model->vertexes[i].position;
		float *converted = &geometry.vertices[3 * (size_t)i];

		if (!isfinite (vertex[0]) || !isfinite (vertex[1]) || !isfinite (vertex[2]))
			goto invalid_geometry;
		converted[0] = -vertex[1] * SA_METERS_PER_UNIT;
		converted[1] = vertex[2] * SA_METERS_PER_UNIT;
		converted[2] = -vertex[0] * SA_METERS_PER_UNIT;
		if (!isfinite (converted[0]) || !isfinite (converted[1]) || !isfinite (converted[2]))
			goto invalid_geometry;
	}

	for (i = 0; i < model->nummodelsurfaces; ++i)
	{
		msurface_t *face = &model->surfaces[model->firstmodelsurface + i];
		const texture_t *texture;
		int first = -1, previous = -1;
		int material;

		qboolean include;
		if (!SpatialWorld_IncludeSurface (face, &include))
			goto invalid_geometry;
		if (!include || face->numedges < 3)
			continue;
		if (!SpatialWorld_ValidPlane (face->plane))
			goto invalid_geometry;
		texture = face->texinfo->texture;
		material = SpatialWorld_Material (texture);
		for (j = 0; j < face->numedges; ++j)
		{
			int vertex_index;

			if (!SpatialWorld_SurfaceVertex (model, face->firstedge + j, &vertex_index))
				goto invalid_geometry;
			if (j >= 2)
			{
				const float *p0 = model->vertexes[first].position;
				const float *p1 = model->vertexes[previous].position;
				const float *p2 = model->vertexes[vertex_index].position;
				double ax = (double)p1[0] - p0[0];
				double ay = (double)p1[1] - p0[1];
				double az = (double)p1[2] - p0[2];
				double bx = (double)p2[0] - p0[0];
				double by = (double)p2[1] - p0[1];
				double bz = (double)p2[2] - p0[2];
				double cx = ay * bz - az * by;
				double cy = az * bx - ax * bz;
				double cz = ax * by - ay * bx;
				double cross_squared = cx * cx + cy * cy + cz * cz;

				if (!isfinite (cross_squared))
					goto invalid_geometry;
				if (cross_squared > 1.0e-6)
				{
					const int triangle_index = geometry.num_triangles;
					double orientation = cx * face->plane->normal[0] +
						cy * face->plane->normal[1] + cz * face->plane->normal[2];
					int *triangle;

					if (face->flags & SURF_PLANEBACK)
						orientation = -orientation;
					if (!isfinite (orientation) || triangle_index >= (int)triangle_capacity)
						goto invalid_geometry;
					triangle = &geometry.triangles[3 * (size_t)triangle_index];
					triangle[0] = first;
					triangle[1] = orientation < 0.0 ? vertex_index : previous;
					triangle[2] = orientation < 0.0 ? previous : vertex_index;
					geometry.materials[triangle_index] = material;
					++geometry.num_triangles;
				}
			}
			if (j == 0)
				first = vertex_index;
			previous = vertex_index;
		}
	}

	if (!geometry.num_triangles)
		goto invalid_geometry;
	Con_Printf ("Room acoustics: %d world triangles, building CPU scene off-thread\n",
		geometry.num_triangles);
	if (!Spatial_ReplaceRoom (&geometry))
	{
		Spatial_ClearWorld ();
		Con_Printf ("Room acoustics: scene replacement failed; direct audio remains available\n");
	}
	return;

oversized_geometry:
	Spatial_ClearWorld ();
	Con_Printf ("Room acoustics: world geometry exceeds the export limit\n");
	return;

allocation_failed:
	free (geometry.vertices);
	free (geometry.triangles);
	free (geometry.materials);
	Spatial_ClearWorld ();
	Con_Printf ("Room acoustics: scene export allocation failed; direct audio remains available\n");
	return;

invalid_geometry:
	free (geometry.vertices);
	free (geometry.triangles);
	free (geometry.materials);
	Spatial_ClearWorld ();
	Con_Printf ("Room acoustics: no usable world geometry or invalid BSP data\n");
}

#endif /* USE_STEAMAUDIO */
