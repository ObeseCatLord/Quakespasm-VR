/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

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
// r_world.c: world model rendering

#include "quakedef.h"

extern cvar_t gl_fullbrights, r_drawflat, gl_overbright, r_oldwater, r_oldwater_max_drawpolys, r_oldskyleaf, r_showtris; //johnfitz

byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);
qboolean R_BackFaceCull(msurface_t *surf);

/*
 * The VR compositor supplies both eye positions before it begins rendering.
 * We only share a candidate set after each rendered eye has proved that it is
 * still in the prepared leaf.  This makes the shared PVS an inclusive union,
 * not a first-eye cache.
 */
extern qboolean R_IsVRTwoEyeFrame(void);

static mleaf_t *r_vr_sharedvis_leaf[2];
static qboolean r_vr_sharedvis_prepared;
static qboolean r_vr_sharedvis_built;
static msurface_t **r_vr_sharedvis_surfaces;
static int r_vr_sharedvis_surface_capacity;
static int r_vr_sharedvis_num_surfaces;
static entity_t **r_vr_sharedvis_entities;
static byte *r_vr_sharedvis_pvs;
static int r_vr_sharedvis_pvs_capacity;
static int r_vr_sharedvis_hits;
static int r_vr_sharedvis_misses;
static int r_vr_sharedvis_fallbacks;
static int r_vr_sharedvis_validation;

/*
 * BSP2 maps without a VIS lump make every leaf visible.  Their marksurface
 * arrays can be much larger than the unique world surface set, so build the
 * latter once per map and retain it as a conservative candidate list.  The
 * per-view surface bounds and backface tests still happen every frame.
 */
static qmodel_t *r_novis_surface_cache_model;
static mleaf_t *r_novis_surface_cache_leafs;
static msurface_t *r_novis_surface_cache_model_surfaces;
static msurface_t **r_novis_surface_cache_marksurfaces;
static int r_novis_surface_cache_numleafs;
static int r_novis_surface_cache_numsurfaces;
static int r_novis_surface_cache_nummarksurfaces;
static qboolean r_novis_surface_cache_oldskyleaf;
static qboolean r_novis_surface_cache_ready;
static msurface_t **r_novis_surface_cache_surfaces;
static int r_novis_surface_cache_capacity;
static int r_novis_surface_cache_num_surfaces;

void R_InvalidateNoVisSurfaceCache(void)
{
	r_novis_surface_cache_model = NULL;
	r_novis_surface_cache_leafs = NULL;
	r_novis_surface_cache_model_surfaces = NULL;
	r_novis_surface_cache_marksurfaces = NULL;
	r_novis_surface_cache_numleafs = 0;
	r_novis_surface_cache_numsurfaces = 0;
	r_novis_surface_cache_nummarksurfaces = 0;
	r_novis_surface_cache_oldskyleaf = false;
	r_novis_surface_cache_ready = false;
	r_novis_surface_cache_num_surfaces = 0;
}

static qboolean R_NoVisSurfaceCacheMatches(qmodel_t *worldmodel,
	qboolean oldskyleaf)
{
	return r_novis_surface_cache_model == worldmodel &&
		r_novis_surface_cache_leafs == worldmodel->leafs &&
		r_novis_surface_cache_model_surfaces == worldmodel->surfaces &&
		r_novis_surface_cache_marksurfaces == worldmodel->marksurfaces &&
		r_novis_surface_cache_numleafs == worldmodel->numleafs &&
		r_novis_surface_cache_numsurfaces == (int)worldmodel->numsurfaces &&
		r_novis_surface_cache_nummarksurfaces == worldmodel->nummarksurfaces &&
		r_novis_surface_cache_oldskyleaf == oldskyleaf;
}

static void R_NoVisSurfaceCacheSetMap(qmodel_t *worldmodel,
	qboolean oldskyleaf)
{
	r_novis_surface_cache_model = worldmodel;
	r_novis_surface_cache_leafs = worldmodel->leafs;
	r_novis_surface_cache_model_surfaces = worldmodel->surfaces;
	r_novis_surface_cache_marksurfaces = worldmodel->marksurfaces;
	r_novis_surface_cache_numleafs = worldmodel->numleafs;
	r_novis_surface_cache_numsurfaces = (int)worldmodel->numsurfaces;
	r_novis_surface_cache_nummarksurfaces = worldmodel->nummarksurfaces;
	r_novis_surface_cache_oldskyleaf = oldskyleaf;
	r_novis_surface_cache_ready = false;
	r_novis_surface_cache_num_surfaces = 0;
}

static qboolean R_EnsureNoVisSurfaceCache(void)
{
	qmodel_t *worldmodel;
	msurface_t **surfaces;
	msurface_t *surf, **mark;
	mleaf_t *leaf;
	byte *seen;
	size_t surfacebytes;
	uintptr_t surfaceoffset;
	int i, j, surfaceindex;
	qboolean oldskyleaf;

	worldmodel = cl.worldmodel;
	oldskyleaf = r_oldskyleaf.value != 0;
	if (worldmodel->numsurfaces > Q_MAXINT)
	{
		Con_Warning("no-VIS surface cache disabled: %u surfaces exceeds cache limit\n",
			worldmodel->numsurfaces);
		return false;
	}

	if (R_NoVisSurfaceCacheMatches(worldmodel, oldskyleaf))
	{
		if (r_novis_surface_cache_ready)
			return true;
		/* Retry a failed build; it may have been due to transient allocation. */
		r_novis_surface_cache_num_surfaces = 0;
	}
	else
	{
		R_NoVisSurfaceCacheSetMap(worldmodel, oldskyleaf);
	}
	if (worldmodel->numsurfaces == 0)
	{
		r_novis_surface_cache_ready = true;
		return true;
	}

	if ((size_t)worldmodel->numsurfaces >
		SIZE_MAX / sizeof(*r_novis_surface_cache_surfaces) ||
		(size_t)worldmodel->numsurfaces >
		SIZE_MAX / sizeof(*worldmodel->surfaces))
		goto overflow;
	surfacebytes = worldmodel->numsurfaces * sizeof(*worldmodel->surfaces);

	if (r_novis_surface_cache_capacity < (int)worldmodel->numsurfaces)
	{
		surfaces = (msurface_t **)realloc(r_novis_surface_cache_surfaces,
			worldmodel->numsurfaces * sizeof(*r_novis_surface_cache_surfaces));
		if (!surfaces)
			goto allocation_failed;
		r_novis_surface_cache_surfaces = surfaces;
		r_novis_surface_cache_capacity = worldmodel->numsurfaces;
	}

	seen = (byte *)calloc(worldmodel->numsurfaces, sizeof(*seen));
	if (!seen)
		goto allocation_failed;

	leaf = &worldmodel->leafs[1];
	for (i = 0; i < worldmodel->numleafs; i++, leaf++)
	{
		if (!oldskyleaf && leaf->contents == CONTENTS_SKY)
			continue;
		for (j = 0, mark = leaf->firstmarksurface;
			j < leaf->nummarksurfaces; j++, mark++)
		{
			surf = *mark;
			surfaceoffset = (uintptr_t)surf - (uintptr_t)worldmodel->surfaces;
			if ((uintptr_t)surf < (uintptr_t)worldmodel->surfaces ||
			surfaceoffset >= surfacebytes ||
			surfaceoffset % sizeof(*worldmodel->surfaces) != 0)
			{
				free(seen);
				Con_Warning("no-VIS surface cache disabled: invalid marksurface\n");
				goto failed;
			}
			surfaceindex = surfaceoffset / sizeof(*worldmodel->surfaces);
			if (!seen[surfaceindex])
			{
				seen[surfaceindex] = true;
				r_novis_surface_cache_surfaces[
					r_novis_surface_cache_num_surfaces++] = surf;
			}
		}
	}

	free(seen);
	r_novis_surface_cache_ready = true;
	return true;

overflow:
	Con_Warning("no-VIS surface cache disabled: allocation size overflow\n");
	goto failed;

allocation_failed:
	Con_Warning("no-VIS surface cache disabled: allocation failed\n");
failed:
	return false;
}

static void R_ChainNoVisSurfaceCache(qboolean perf)
{
	msurface_t *surf;
	int i;

	for (i = 0; i < r_novis_surface_cache_num_surfaces; i++)
	{
		surf = r_novis_surface_cache_surfaces[i];
		surf->visframe = r_visframecount;
		if (!R_CullBox(surf->mins, surf->maxs) && !R_BackFaceCull(surf))
		{
			rs_brushpolys++;
			R_ChainSurface(surf, chain_world);
			R_RenderDynamicLightmaps(surf);
			if (perf)
				r_perf_surfaces_chained++;
			if (surf->texinfo->texture->warpimage)
				surf->texinfo->texture->update_warp = true;
		}
		else if (perf)
			r_perf_surfaces_culled++;
	}

	if (perf)
		r_perf_surfaces_unique += r_novis_surface_cache_num_surfaces;
}

static void R_VRSharedVisibilityInvalidate(qboolean validation)
{
	r_vr_sharedvis_built = false;
	r_vr_sharedvis_prepared = false;
	r_vr_sharedvis_fallbacks++;
	if (validation)
		r_vr_sharedvis_validation++;
}

void R_PrepareVRStereoVisibility(const vec_t *eye0, const vec_t *eye1)
{
	r_vr_sharedvis_prepared = false;
	r_vr_sharedvis_built = false;

	if (!cl.worldmodel || !eye0 || !eye1)
		return;

	r_vr_sharedvis_leaf[0] = Mod_PointInLeaf((vec_t *)eye0, cl.worldmodel);
	r_vr_sharedvis_leaf[1] = Mod_PointInLeaf((vec_t *)eye1, cl.worldmodel);
	r_vr_sharedvis_prepared = true;
}

void R_BeginVRStereoVisibility(void)
{
	r_vr_sharedvis_built = false;
	r_vr_sharedvis_num_surfaces = 0;
	r_vr_sharedvis_hits = r_vr_sharedvis_misses = 0;
	r_vr_sharedvis_fallbacks = r_vr_sharedvis_validation = 0;
}

void R_EndVRStereoVisibility(void)
{
	r_vr_sharedvis_prepared = false;
	r_vr_sharedvis_built = false;
	r_vr_sharedvis_num_surfaces = 0;
	r_vr_sharedvis_hits = r_vr_sharedvis_misses = 0;
	r_vr_sharedvis_fallbacks = r_vr_sharedvis_validation = 0;
}

void R_GetVRStereoVisibilityStats(int *hits, int *misses, int *fallbacks,
								 int *validation)
{
	if (hits)
		*hits = r_vr_sharedvis_hits;
	if (misses)
		*misses = r_vr_sharedvis_misses;
	if (fallbacks)
		*fallbacks = r_vr_sharedvis_fallbacks;
	if (validation)
		*validation = r_vr_sharedvis_validation;
}

static qboolean R_VRSharedVisibilityLeafHasWaterPortal(mleaf_t *viewleaf)
{
	msurface_t **mark;
	int i;

	for (i = 0, mark = viewleaf->firstmarksurface;
		 i < viewleaf->nummarksurfaces; i++, mark++)
		if ((*mark)->flags & SURF_DRAWTURB)
			return true;

	return false;
}

static qboolean R_VRSharedVisibilityEnsureStorage(int pvsbytes)
{
	msurface_t **surfaces;
	entity_t **entities;
	byte *pvs;

	if (r_vr_sharedvis_surface_capacity < (int)cl.worldmodel->numsurfaces)
	{
		surfaces = (msurface_t **)realloc(
			r_vr_sharedvis_surfaces,
			cl.worldmodel->numsurfaces * sizeof(*r_vr_sharedvis_surfaces));
		if (!surfaces)
			return false;
		r_vr_sharedvis_surfaces = surfaces;
		r_vr_sharedvis_surface_capacity = cl.worldmodel->numsurfaces;
	}

	if (!r_vr_sharedvis_entities)
	{
		entities = (entity_t **)malloc(MAX_VISEDICTS * sizeof(*entities));
		if (!entities)
			return false;
		r_vr_sharedvis_entities = entities;
	}

	if (r_vr_sharedvis_pvs_capacity < pvsbytes)
	{
		pvs = (byte *)realloc(r_vr_sharedvis_pvs, pvsbytes);
		if (!pvs)
			return false;
		r_vr_sharedvis_pvs = pvs;
		r_vr_sharedvis_pvs_capacity = pvsbytes;
	}

	return true;
}

static qboolean R_VRSharedVisibilityAddEntity(entity_t *ent, int *count)
{
	int i;

	if (ent->visframe == r_framecount)
		return true;

	for (i = 0; i < *count; i++)
		if (r_vr_sharedvis_entities[i] == ent)
			return true;

	if (cl_numvisedicts + *count >= MAX_VISEDICTS)
		return false;

	r_vr_sharedvis_entities[(*count)++] = ent;
	return true;
}

static qboolean R_VRSharedVisibilityCollectEfrags(efrag_t *efrag,
													 int *count)
{
	for (; efrag; efrag = efrag->leafnext)
		if (!R_VRSharedVisibilityAddEntity(efrag->entity, count))
			return false;

	return true;
}

static void R_VRSharedVisibilityChainSurfaces(qboolean perf)
{
	msurface_t *surf;
	int i;

	for (i = 0; i < lightmap_count; i++)
		lightmaps[i].polys = NULL;
	for (i = 0; i < cl.worldmodel->numtextures; i++)
		if (cl.worldmodel->textures[i])
			cl.worldmodel->textures[i]->texturechains[chain_world] = NULL;

	for (i = 0; i < r_vr_sharedvis_num_surfaces; i++)
	{
		surf = r_vr_sharedvis_surfaces[i];
		if (!R_CullBox(surf->mins, surf->maxs) && !R_BackFaceCull(surf))
		{
			rs_brushpolys++;
			R_ChainSurface(surf, chain_world);
			R_RenderDynamicLightmaps(surf);
			if (perf)
				r_perf_surfaces_chained++;
			if (surf->texinfo->texture->warpimage)
				surf->texinfo->texture->update_warp = true;
		}
		else if (perf)
			r_perf_surfaces_culled++;
	}
}

static qboolean R_BuildVRStereoVisibility(qboolean perf)
{
	byte *vis0, *vis1;
	mleaf_t *leaf;
	msurface_t *surf, **mark;
	int entity_count, i, j, pvsbytes;
	qboolean cache, novis;

	if (R_VRSharedVisibilityLeafHasWaterPortal(r_vr_sharedvis_leaf[0]) ||
		R_VRSharedVisibilityLeafHasWaterPortal(r_vr_sharedvis_leaf[1]))
		return false;

	pvsbytes = (cl.worldmodel->numleafs + 7) >> 3;
	if (!R_VRSharedVisibilityEnsureStorage(pvsbytes))
		return false;
	cache = cl.worldmodel->visdata == NULL && R_EnsureNoVisSurfaceCache();

	novis = r_novis.value ||
		r_vr_sharedvis_leaf[0]->contents == CONTENTS_SOLID ||
		r_vr_sharedvis_leaf[0]->contents == CONTENTS_SKY ||
		r_vr_sharedvis_leaf[1]->contents == CONTENTS_SOLID ||
		r_vr_sharedvis_leaf[1]->contents == CONTENTS_SKY;
	if (novis)
	{
		vis0 = Mod_NoVisPVS(cl.worldmodel);
		vis1 = vis0;
		if (perf)
			r_perf_pvs_novis += 2;
	}
	else
	{
		vis0 = Mod_LeafPVS(r_vr_sharedvis_leaf[0], cl.worldmodel);
		memcpy(r_vr_sharedvis_pvs, vis0, pvsbytes);
		vis1 = Mod_LeafPVS(r_vr_sharedvis_leaf[1], cl.worldmodel);
		if (perf)
			r_perf_pvs_leaf += 2;
	}

	if (novis)
		memcpy(r_vr_sharedvis_pvs, vis0, pvsbytes);
	else
		for (i = 0; i < pvsbytes; i++)
			r_vr_sharedvis_pvs[i] |= vis1[i];

	r_visframecount++;
	r_vr_sharedvis_num_surfaces = 0;
	entity_count = 0;
	leaf = &cl.worldmodel->leafs[1];
	for (i = 0; i < cl.worldmodel->numleafs; i++, leaf++)
	{
		if (perf)
			r_perf_leaves_scanned++;
		if (!(r_vr_sharedvis_pvs[i >> 3] & (1 << (i & 7))))
			continue;

		if (perf)
			r_perf_leaves_visible++;
		if (!cache && (r_oldskyleaf.value || leaf->contents != CONTENTS_SKY))
		{
			if (perf)
				r_perf_marksurfaces_scanned += leaf->nummarksurfaces;
			for (j = 0, mark = leaf->firstmarksurface; j < leaf->nummarksurfaces;
				 j++, mark++)
			{
				surf = *mark;
				if (surf->visframe != r_visframecount)
				{
					surf->visframe = r_visframecount;
					r_vr_sharedvis_surfaces[r_vr_sharedvis_num_surfaces++] = surf;
					if (perf)
						r_perf_surfaces_unique++;
				}
			}
		}

		if (leaf->efrags &&
			!R_VRSharedVisibilityCollectEfrags(leaf->efrags, &entity_count))
			return false;
		if (perf && leaf->efrags)
			r_perf_efrag_leaves++;
	}

	if (cache)
	{
		r_vr_sharedvis_num_surfaces = r_novis_surface_cache_num_surfaces;
		for (i = 0; i < r_vr_sharedvis_num_surfaces; i++)
		{
			surf = r_novis_surface_cache_surfaces[i];
			surf->visframe = r_visframecount;
			r_vr_sharedvis_surfaces[i] = surf;
		}
		if (perf)
			r_perf_surfaces_unique += r_vr_sharedvis_num_surfaces;
	}

	for (i = 0; i < entity_count; i++)
	{
		cl_visedicts[cl_numvisedicts++] = r_vr_sharedvis_entities[i];
		r_vr_sharedvis_entities[i]->visframe = r_framecount;
	}

	r_vr_sharedvis_built = true;
	return true;
}

//==============================================================================
//
// SETUP CHAINS
//
//==============================================================================

/*
================
R_ClearTextureChains -- ericw

clears texture chains for all textures used by the given model, and also
clears the lightmap chains
================
*/
void R_ClearTextureChains (qmodel_t *mod, texchain_t chain)
{
	int i;

	// set all chains to null
	for (i=0 ; i<mod->numtextures ; i++)
		if (mod->textures[i])
			mod->textures[i]->texturechains[chain] = NULL;

	// clear lightmap chains
	for (i=0 ; i<lightmap_count ; i++)
		lightmaps[i].polys = NULL;
}

/*
================
R_ChainSurface -- ericw -- adds the given surface to its texture chain
================
*/
void R_ChainSurface (msurface_t *surf, texchain_t chain)
{
	surf->texturechain = surf->texinfo->texture->texturechains[chain];
	surf->texinfo->texture->texturechains[chain] = surf;
}

/*
================
R_BackFaceCull -- johnfitz -- returns true if the surface is facing away from vieworg
================
*/
qboolean R_BackFaceCull (msurface_t *surf)
{
	double dot;

	if (surf->plane->type < 3)
		dot = r_refdef.vieworg[surf->plane->type] - surf->plane->dist;
	else
		dot = DotProduct (r_refdef.vieworg, surf->plane->normal) - surf->plane->dist;

	if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK))
		return true;

	return false;
}

/*
===============
R_MarkSurfaces -- johnfitz -- mark surfaces based on PVS and rebuild texture chains
===============
*/
void R_MarkSurfaces (void)
{
	byte		*vis;
	mleaf_t		*leaf;
	msurface_t	*surf, **mark;
	int			i, j;
	qboolean	cache, nearwaterportal;
	qboolean	perf;

	perf = r_perfdebug.value != 0;

	/*
	 * A shared set is only valid for the normal two-eye world pass.  The
	 * prepared leaves are checked against the actual, fully calculated view
	 * origin before use; any mismatch takes the original code path below.
	 */
	if (R_IsVRTwoEyeFrame() && r_vr_sharedvis_prepared && !skyroom_drawing)
	{
		if (R_IsVRFirstEye())
		{
			if (r_viewleaf != r_vr_sharedvis_leaf[0])
			{
				R_VRSharedVisibilityInvalidate(true);
			}
			else if (r_vr_sharedvis_built)
			{
				R_VRSharedVisibilityChainSurfaces(perf);
				r_vr_sharedvis_hits++;
				return;
			}
			else
			{
				r_vr_sharedvis_misses++;
				if (R_BuildVRStereoVisibility(perf))
				{
					R_VRSharedVisibilityChainSurfaces(perf);
					return;
				}
				R_VRSharedVisibilityInvalidate(false);
			}
		}
		else if (r_vr_sharedvis_built &&
			 r_viewleaf == r_vr_sharedvis_leaf[1])
		{
			R_VRSharedVisibilityChainSurfaces(perf);
			r_vr_sharedvis_hits++;
			return;
		}
		else
		{
			R_VRSharedVisibilityInvalidate(true);
		}
	}

	// clear lightmap chains
	for (i=0 ; i<lightmap_count ; i++)
		lightmaps[i].polys = NULL;

	// check this leaf for water portals
	// TODO: loop through all water surfs and use distance to leaf cullbox
	nearwaterportal = false;
	for (i=0, mark = r_viewleaf->firstmarksurface; i < r_viewleaf->nummarksurfaces; i++, mark++)
		if ((*mark)->flags & SURF_DRAWTURB)
			nearwaterportal = true;

	// choose vis data
	if (r_novis.value || r_viewleaf->contents == CONTENTS_SOLID || r_viewleaf->contents == CONTENTS_SKY)
	{
		vis = Mod_NoVisPVS (cl.worldmodel);
		if (perf)
			r_perf_pvs_novis++;
	}
	else if (nearwaterportal)
	{
		vis = SV_FatPVS (r_origin, cl.worldmodel);
		if (perf)
			r_perf_pvs_fat++;
	}
	else
	{
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);
		if (perf)
			r_perf_pvs_leaf++;
	}
	cache = cl.worldmodel->visdata == NULL && !nearwaterportal &&
		!skyroom_drawing &&
		R_EnsureNoVisSurfaceCache();

	r_visframecount++;

	// set all chains to null
	for (i=0 ; i<cl.worldmodel->numtextures ; i++)
		if (cl.worldmodel->textures[i])
			cl.worldmodel->textures[i]->texturechains[chain_world] = NULL;

	// iterate through leaves, marking surfaces
	leaf = &cl.worldmodel->leafs[1];
	for (i=0 ; i<cl.worldmodel->numleafs ; i++, leaf++)
	{
		if (perf)
			r_perf_leaves_scanned++;
		if (vis[i>>3] & (1<<(i&7)))
		{
			if (perf)
				r_perf_leaves_visible++;
			if (R_CullBox(leaf->minmaxs, leaf->minmaxs + 3))
			{
				if (perf)
					r_perf_leaves_culled++;
				continue;
			}

			if (!cache && (r_oldskyleaf.value || leaf->contents != CONTENTS_SKY))
			{
				if (perf)
					r_perf_marksurfaces_scanned += leaf->nummarksurfaces;
				for (j=0, mark = leaf->firstmarksurface; j<leaf->nummarksurfaces; j++, mark++)
				{
					surf = *mark;
					if (surf->visframe != r_visframecount)
					{
						if (perf)
							r_perf_surfaces_unique++;
						surf->visframe = r_visframecount;
						if (!R_CullBox(surf->mins, surf->maxs) && !R_BackFaceCull (surf))
						{
							rs_brushpolys++; //count wpolys here
							R_ChainSurface(surf, chain_world);
							R_RenderDynamicLightmaps(surf);
							if (perf)
								r_perf_surfaces_chained++;
							if (surf->texinfo->texture->warpimage)
								surf->texinfo->texture->update_warp = true;
						}
						else if (perf)
							r_perf_surfaces_culled++;
					}
				}
			}

			// add static models
			if (leaf->efrags)
			{
				if (perf)
					r_perf_efrag_leaves++;
				R_StoreEfrags (&leaf->efrags);
			}
		}
	}

	if (cache)
		R_ChainNoVisSurfaceCache(perf);
}

//==============================================================================
//
// DRAW CHAINS
//
//==============================================================================

/*
=============
R_BeginTransparentDrawing -- ericw
=============
*/
static void R_BeginTransparentDrawing (float entalpha)
{
	if (entalpha < 1.0f)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f (1,1,1,entalpha);
	}
}

/*
=============
R_EndTransparentDrawing -- ericw
=============
*/
static void R_EndTransparentDrawing (float entalpha)
{
	if (entalpha < 1.0f)
	{
		glDepthMask (GL_TRUE);
		glDisable (GL_BLEND);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor3f (1, 1, 1);
	}
}

/*
================
R_DrawTextureChains_ShowTris -- johnfitz
================
*/
void R_DrawTextureChains_ShowTris (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	glpoly_t	*p;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];
		if (!t)
			continue;

		if (r_oldwater.value && t->texturechains[chain] && (t->texturechains[chain]->flags & SURF_DRAWTURB))
		{
			for (s = t->texturechains[chain]; s; s = s->texturechain)
				for (p = s->polys->next; p; p = p->next)
				{
					DrawGLTriangleFan (p);
				}
		}
		else
		{
			for (s = t->texturechains[chain]; s; s = s->texturechain)
			{
				DrawGLTriangleFan (s->polys);
			}
		}
	}
}

/*
================
R_DrawTextureChains_Drawflat -- johnfitz
================
*/
void R_DrawTextureChains_Drawflat (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	glpoly_t	*p;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];
		if (!t)
			continue;

		if (r_oldwater.value && t->texturechains[chain] && (t->texturechains[chain]->flags & SURF_DRAWTURB))
		{
			for (s = t->texturechains[chain]; s; s = s->texturechain)
				for (p = s->polys->next; p; p = p->next)
				{
					srand((unsigned int) (uintptr_t) p);
					glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
					DrawGLPoly (p);
					rs_brushpasses++;
				}
		}
		else
		{
			for (s = t->texturechains[chain]; s; s = s->texturechain)
			{
				srand((unsigned int) (uintptr_t) s->polys);
				glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
				DrawGLPoly (s->polys);
				rs_brushpasses++;
			}
		}
	}
	glColor3f (1,1,1);
	srand ((int) (cl.time * 1000));
}

/*
================
R_DrawTextureChains_Glow -- johnfitz
================
*/
void R_DrawTextureChains_Glow (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	gltexture_t	*glt;
	qboolean	bound;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(glt = R_TextureAnimation(t, ent != NULL ? ent->frame : 0)->fullbright))
			continue;

		bound = false;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_Bind (glt);
				bound = true;
			}
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
	}
}

//==============================================================================
//
// VBO SUPPORT
//
//==============================================================================

static int R_NumTriangleIndicesForSurf (msurface_t *s)
{
	return 3 * (s->numedges - 2);
}

#define MAX_BATCH_SIZE 65536
#define MAX_BATCH_SURFACES (MAX_BATCH_SIZE / 3)

static unsigned int num_vbo_indices;
static GLsizei vbo_batch_counts[MAX_BATCH_SURFACES];
static const GLvoid *vbo_batch_offsets[MAX_BATCH_SURFACES];
static unsigned int num_vbo_batch_surfaces;

/*
================
R_ClearBatch
================
*/
static void R_ClearBatch (void)
{
	num_vbo_indices = 0;
	num_vbo_batch_surfaces = 0;
}

/*
================
R_FlushBatch

Draw the current texture/lightmap batch from persistent EBO ranges.
================
*/
static void R_FlushBatch (void)
{
	if (num_vbo_indices > 0)
	{
		if (r_perfdebug.value != 0.0f)
		{
			const int batch_surfaces = (int)num_vbo_batch_surfaces;

			r_perf_world_batch_flushes++;
			r_perf_world_batch_surfaces += batch_surfaces;
			if (batch_surfaces > r_perf_world_batch_max_surfaces)
				r_perf_world_batch_max_surfaces = batch_surfaces;
		}

		if (GL_MultiDrawElementsFunc)
		{
			if (r_perfdebug.value != 0.0f)
				r_perf_world_batch_mdraw_calls++;
			GL_MultiDrawElementsFunc (GL_TRIANGLES, vbo_batch_counts,
				GL_UNSIGNED_INT, vbo_batch_offsets, num_vbo_batch_surfaces);
		}
		else
		{
			unsigned int i;

			for (i = 0; i < num_vbo_batch_surfaces; i++)
			{
				if (r_perfdebug.value != 0.0f)
					r_perf_world_batch_draw_calls++;
				glDrawElements (GL_TRIANGLES, vbo_batch_counts[i], GL_UNSIGNED_INT,
					vbo_batch_offsets[i]);
			}
		}
		num_vbo_indices = 0;
		num_vbo_batch_surfaces = 0;
	}
}

/*
================
R_BatchSurface

Add a surface to the current EBO batch.
================
*/
static void R_BatchSurface (msurface_t *s)
{
	int num_surf_indices = R_NumTriangleIndicesForSurf (s);

	if (num_surf_indices <= 0)
	{
	//	Con_DWarning ("bad numedges for surface\n");
		return;
	}

	if (s->vbo_firstindex < 0)
		Host_Error ("R_BatchSurface: invalid EBO offset");

	if (num_surf_indices > MAX_BATCH_SIZE)
	{
		R_FlushBatch ();
		if (r_perfdebug.value != 0.0f)
			r_perf_world_batch_draw_calls++;
		glDrawElements (GL_TRIANGLES, num_surf_indices, GL_UNSIGNED_INT,
			(const GLvoid *)(uintptr_t)((size_t)s->vbo_firstindex * sizeof(unsigned int)));
		return;
	}

	if (num_vbo_indices + num_surf_indices > MAX_BATCH_SIZE ||
		num_vbo_batch_surfaces == MAX_BATCH_SURFACES)
		R_FlushBatch ();

	vbo_batch_counts[num_vbo_batch_surfaces] = num_surf_indices;
	vbo_batch_offsets[num_vbo_batch_surfaces] =
		(const GLvoid *)(uintptr_t)((size_t)s->vbo_firstindex * sizeof(unsigned int));
	num_vbo_batch_surfaces++;
	num_vbo_indices += num_surf_indices;
}

/*
================
R_DrawTextureChains_Multitexture -- johnfitz
================
*/
void R_DrawTextureChains_Multitexture (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i, j;
	msurface_t	*s;
	texture_t	*t;
	float		*v;
	qboolean	bound;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

		bound = false;
		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_Bind ((R_TextureAnimation(t, ent != NULL ? ent->frame : 0))->gltexture);
					
				if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
					glEnable (GL_ALPHA_TEST); // Flip alpha test back on
					
				GL_EnableMultitexture(); // selects TEXTURE1
				bound = true;
			}
			GL_Bind (lightmaps[s->lightmaptexturenum].texture);
			glBegin(GL_POLYGON);
			v = s->polys->verts[0];
			for (j=0 ; j<s->polys->numverts ; j++, v+= VERTEXSIZE)
			{
				GL_MTexCoord2fFunc (GL_TEXTURE0_ARB, v[3], v[4]);
				GL_MTexCoord2fFunc (GL_TEXTURE1_ARB, v[5], v[6]);
				glVertex3fv (v);
			}
			glEnd ();
			rs_brushpasses++;
		}
		GL_DisableMultitexture(); // selects TEXTURE0

		if (bound && t->texturechains[chain]->flags & SURF_DRAWFENCE)
			glDisable (GL_ALPHA_TEST); // Flip alpha test back off
	}
}

/*
================
R_DrawTextureChains_NoTexture -- johnfitz

draws surfs whose textures were missing from the BSP
================
*/
void R_DrawTextureChains_NoTexture (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	qboolean	bound;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_NOTEXTURE))
			continue;

		bound = false;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_Bind (t->gltexture);
				bound = true;
			}
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
	}
}

/*
================
R_DrawTextureChains_TextureOnly -- johnfitz
================
*/
void R_DrawTextureChains_TextureOnly (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	qboolean	bound;

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
			continue;

		bound = false;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_Bind ((R_TextureAnimation(t, ent != NULL ? ent->frame : 0))->gltexture);
					
				if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
					glEnable (GL_ALPHA_TEST); // Flip alpha test back on
					
				bound = true;
			}
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}

		if (bound && t->texturechains[chain]->flags & SURF_DRAWFENCE)
			glDisable (GL_ALPHA_TEST); // Flip alpha test back off
	}
}

/*
================
GL_WaterAlphaForEntitySurface -- ericw

Returns the water alpha to use for the entity and surface combination.
================
*/
float GL_WaterAlphaForEntitySurface (entity_t *ent, msurface_t *s)
{
	float entalpha;
	if (ent == NULL || ent->alpha == ENTALPHA_DEFAULT)
		entalpha = GL_WaterAlphaForSurface(s);
	else
		entalpha = ENTALPHA_DECODE(ent->alpha);
	return entalpha;
}

static GLuint r_world_program;
extern GLuint gl_bmodel_vbo;
extern GLuint gl_bmodel_ebo;

// uniforms used in vert shader

// uniforms used in frag shader
static GLint  texLoc;
static GLint  LMTexLoc;
static GLint  fullbrightTexLoc;
static GLint  useFullbrightTexLoc;
static GLint  useOverbrightLoc;
static GLint  useAlphaTestLoc;
static GLint  useLightmapWideLoc;
static GLint  useLightmapOnlyLoc;
static GLint  alphaLoc;

#define vertAttrIndex 0
#define texCoordsAttrIndex 1
#define LMCoordsAttrIndex 2

/*
================
R_UseOldWaterForChain

Large maps can turn oldwater into thousands of immediate-mode draw calls. Keep
legacy oldwater available for small scenes, but fall back to the warp-texture
path once the visible oldwater poly count is high enough to become pathological.
================
*/
static qboolean R_UseOldWaterForChain (qmodel_t *model, texchain_t chain)
{
	static qboolean warned;
	int			i;
	int			drawpolys;
	int			max_drawpolys;
	msurface_t	*s;
	texture_t	*t;
	glpoly_t	*p;

	if (!r_oldwater.value)
		return false;

	max_drawpolys = (int)r_oldwater_max_drawpolys.value;
	if (max_drawpolys <= 0)
		return true;

	drawpolys = 0;
	for (i = 0; i < model->numtextures; i++)
	{
		t = model->textures[i];
		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
			continue;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!s->polys)
				continue;
			for (p = s->polys->next; p; p = p->next)
			{
				if (++drawpolys > max_drawpolys)
				{
					if (!warned)
					{
						Con_DPrintf ("r_oldwater: using fast warp textures for %s (%d visible oldwater draw polys > %d)\n",
							model->name, drawpolys, max_drawpolys);
						warned = true;
					}
					return false;
				}
			}
		}
	}

	return true;
}

/*
================
R_DrawTextureChains_Water -- johnfitz
================
*/
void R_DrawTextureChains_Water (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;
	glpoly_t	*p;
	qboolean	bound;
	float entalpha;
	int		lastlightmap;
	qboolean	has_lit_water;
	qboolean	has_unlit_water;

	if (r_drawflat_cheatsafe || r_lightmap_cheatsafe) // ericw -- !r_drawworld_cheatsafe check moved to R_DrawWorld_Water ()
		return;

	has_lit_water = false;
	has_unlit_water = false;

	if (R_UseOldWaterForChain (model, chain))
	{
		for (i=0 ; i<model->numtextures ; i++)
		{
			t = model->textures[i];
			if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
				continue;
			bound = false;
			entalpha = 1.0f;
			for (s = t->texturechains[chain]; s; s = s->texturechain)
			{
				if (!bound) //only bind once we are sure we need this texture
				{
					entalpha = GL_WaterAlphaForEntitySurface (ent, s);
					R_BeginTransparentDrawing (entalpha);
					GL_Bind (t->gltexture);
					bound = true;
				}
				for (p = s->polys->next; p; p = p->next)
				{
					DrawWaterPoly (p);
					rs_brushpasses++;
				}
			}
			R_EndTransparentDrawing (entalpha);
		}
	}
	else if (cl.worldmodel->haslitwater && r_litwater.value && r_world_program != 0)
	{
		const int overbright = !!gl_overbright.value;
		const int wide10bits = !!r_lightmapwide.value;

		has_lit_water = true;

		GL_UseProgram (r_world_program);

		// Bind the buffers
		GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
		GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, gl_bmodel_ebo);

		GL_EnableVertexAttribArrayFunc (vertAttrIndex);
		GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
		GL_EnableVertexAttribArrayFunc (LMCoordsAttrIndex);

		GL_VertexAttribPointerFunc (vertAttrIndex,      3, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0));
		GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 3);
		GL_VertexAttribPointerFunc (LMCoordsAttrIndex,  2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 5);

		// Set uniforms
		GL_Uniform1iFunc (texLoc, 0);
		GL_Uniform1iFunc (LMTexLoc, 1);
		GL_Uniform1iFunc (fullbrightTexLoc, 2);
		GL_Uniform1iFunc (useFullbrightTexLoc, 0);
		GL_Uniform1iFunc (useOverbrightLoc, overbright);
		GL_Uniform1iFunc (useAlphaTestLoc, 0);
		GL_Uniform1iFunc (useLightmapWideLoc, wide10bits);
		GL_Uniform1iFunc (useLightmapOnlyLoc, 0);

		for (i=0 ; i<model->numtextures ; i++)
		{
			t = model->textures[i];

			if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
				continue;

			if (t->texturechains[chain]->texinfo->flags & TEX_SPECIAL)
			{
				has_unlit_water = true;
				continue;
			}

			bound = false;
			entalpha = 1.0f;
			lastlightmap = 0;
			for (s = t->texturechains[chain]; s; s = s->texturechain)
			{
				if (!bound) //only bind once we are sure we need this texture
				{
					entalpha = GL_WaterAlphaForEntitySurface (ent, s);
					if (entalpha < 1.0f)
					{
						GL_Uniform1fFunc (alphaLoc, entalpha);
						R_BeginTransparentDrawing (entalpha);
					}

					GL_SelectTexture (GL_TEXTURE0);
					GL_Bind (t->warpimage);

					if (model != cl.worldmodel)
					{
						// ericw -- this is copied from R_DrawSequentialPoly.
						// If the poly is not part of the world we have to
						// set this flag
						t->update_warp = true; // FIXME: one frame too late!
					}

					bound = true;
					lastlightmap = s->lightmaptexturenum;
				}

				if (s->lightmaptexturenum != lastlightmap)
					R_FlushBatch ();

				GL_SelectTexture (GL_TEXTURE1);
				GL_Bind (lightmaps[s->lightmaptexturenum].texture);
				lastlightmap = s->lightmaptexturenum;
				R_BatchSurface (s);

				rs_brushpasses++;
			}
			R_FlushBatch ();
			R_EndTransparentDrawing (entalpha);
		}

		// clean up
		GL_DisableVertexAttribArrayFunc (vertAttrIndex);
		GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
		GL_DisableVertexAttribArrayFunc (LMCoordsAttrIndex);
		GL_UseProgram (0);
		GL_SelectTexture (GL_TEXTURE0);
	}
	else
		has_unlit_water = true;

	if (has_unlit_water)
	{
		// Unlit water
		for (i=0 ; i<model->numtextures ; i++)
		{
			t = model->textures[i];
			if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
				continue;
			if (has_lit_water && !(t->texturechains[chain]->texinfo->flags & TEX_SPECIAL))
				continue;
			bound = false;
			entalpha = 1.0f;
			for (s = t->texturechains[chain]; s; s = s->texturechain)
			{
				if (!bound) //only bind once we are sure we need this texture
				{
					entalpha = GL_WaterAlphaForEntitySurface (ent, s);
					R_BeginTransparentDrawing (entalpha);
					GL_Bind (t->warpimage);

					if (model != cl.worldmodel)
					{
						// ericw -- this is copied from R_DrawSequentialPoly.
						// If the poly is not part of the world we have to
						// set this flag
						t->update_warp = true; // FIXME: one frame too late!
					}

					bound = true;
				}
				DrawGLPoly (s->polys);
				rs_brushpasses++;
			}
			R_EndTransparentDrawing (entalpha);
		}
	}
}

/*
================
R_DrawTextureChains_White -- johnfitz -- draw sky and water as white polys when r_lightmap is 1
================
*/
void R_DrawTextureChains_White (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t	*s;
	texture_t	*t;

	glDisable (GL_TEXTURE_2D);
	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTILED))
			continue;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			DrawGLPoly (s->polys);
			rs_brushpasses++;
		}
	}
	glEnable (GL_TEXTURE_2D);
}

/*
================
R_DrawLightmapChains -- johnfitz -- R_BlendLightmaps stripped down to almost nothing
================
*/
void R_DrawLightmapChains (void)
{
	int			i, j;
	glpoly_t	*p;
	float		*v;

	for (i=0 ; i<lightmap_count ; i++)
	{
		if (!lightmaps[i].polys)
			continue;

		GL_Bind (lightmaps[i].texture);
		for (p = lightmaps[i].polys; p; p=p->chain)
		{
			glBegin (GL_POLYGON);
			v = p->verts[0];
			for (j=0 ; j<p->numverts ; j++, v+= VERTEXSIZE)
			{
				glTexCoord2f (v[5], v[6]);
				glVertex3fv (v);
			}
			glEnd ();
			rs_brushpasses++;
		}
	}
}

/*
=============
GLWorld_CreateShaders
=============
*/
void GLWorld_CreateShaders (void)
{
	const glsl_attrib_binding_t bindings[] = {
		{ "Vert", vertAttrIndex },
		{ "TexCoords", texCoordsAttrIndex },
		{ "LMCoords", LMCoordsAttrIndex }
	};

	// Driver bug workarounds:
	// - "Intel(R) UHD Graphics 600" version "4.6.0 - Build 26.20.100.7263"
	//    crashing on glUseProgram with `vec3 Vert` and
	//    `gl_ModelViewProjectionMatrix * vec4(Vert, 1.0);`. Work around with
	//    making Vert a vec4. (https://sourceforge.net/p/quakespasm/bugs/39/)
	const GLchar *vertSource = \
		"#version 110\n"
		"\n"
		"attribute vec4 Vert;\n"
		"attribute vec2 TexCoords;\n"
		"attribute vec2 LMCoords;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		"	gl_TexCoord[0] = vec4(TexCoords, 0.0, 0.0);\n"
		"	gl_TexCoord[1] = vec4(LMCoords, 0.0, 0.0);\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * Vert;\n"
		"	FogFragCoord = gl_Position.w;\n"
		"}\n";
	
	const GLchar *fragSource = \
		"#version 110\n"
		"\n"
		"uniform sampler2D Tex;\n"
		"uniform sampler2D LMTex;\n"
		"uniform sampler2D FullbrightTex;\n"
		"uniform bool UseFullbrightTex;\n"
		"uniform bool UseOverbright;\n"
		"uniform bool UseAlphaTest;\n"
		"uniform bool UseLightmapWide;\n"
		"uniform bool UseLightmapOnly;\n"
		"uniform float Alpha;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		"	vec4 result = texture2D(Tex, gl_TexCoord[0].xy);\n"
		"	if (UseLightmapOnly)\n"
		"		result = vec4(0.5, 0.5, 0.5, 1.0);\n"
		"	if (UseAlphaTest && (result.a < 0.666))\n"
		"		discard;\n"
		"	result *= texture2D(LMTex, gl_TexCoord[1].xy);\n"
		"	if (UseLightmapWide)\n"
		"	    result.rgb *= 4.0;\n"
		"	if (UseOverbright)\n"
		"		result.rgb *= 2.0;\n"
		"	if (UseFullbrightTex)\n"
		"		result += texture2D(FullbrightTex, gl_TexCoord[0].xy);\n"
		"	result = clamp(result, 0.0, 1.0);\n"
		"	float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
		"	fog = clamp(fog, 0.0, 1.0);\n"
		"	result = mix(gl_Fog.color, result, fog);\n"
		"	result.a = Alpha;\n" // FIXME: This will make almost transparent things cut holes though heavy fog
		"	gl_FragColor = result;\n"
		"}\n";

	if (!gl_glsl_alias_able)
		return;

	r_world_program = GL_CreateProgram (vertSource, fragSource, Q_COUNTOF(bindings), bindings);
	
	if (r_world_program != 0)
	{
		// get uniform locations
		texLoc = GL_GetUniformLocation (&r_world_program, "Tex");
		LMTexLoc = GL_GetUniformLocation (&r_world_program, "LMTex");
		fullbrightTexLoc = GL_GetUniformLocation (&r_world_program, "FullbrightTex");
		useFullbrightTexLoc = GL_GetUniformLocation (&r_world_program, "UseFullbrightTex");
		useOverbrightLoc = GL_GetUniformLocation (&r_world_program, "UseOverbright");
		useAlphaTestLoc = GL_GetUniformLocation (&r_world_program, "UseAlphaTest");
		useLightmapWideLoc = GL_GetUniformLocation (&r_world_program, "UseLightmapWide");
		useLightmapOnlyLoc = GL_GetUniformLocation (&r_world_program, "UseLightmapOnly");
		alphaLoc = GL_GetUniformLocation (&r_world_program, "Alpha");
	}
}

/*
================
R_DrawTextureChains_GLSL -- ericw

Draw lightmapped surfaces with fulbrights in one pass, using VBO.
Requires 3 TMUs, OpenGL 2.0
================
*/
void R_DrawTextureChains_GLSL (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	const float	entalpha = (ent != NULL) ?
			 ENTALPHA_DECODE(ent->alpha) : 1.0f;
	const int	overbright = !!gl_overbright.value;
	const int	wide10bits = !!r_lightmapwide.value;

	int			i;
	msurface_t	*s;
	texture_t	*t;
	qboolean	bound;
	int		lastlightmap;
	gltexture_t	*fullbright = NULL;

// enable blending / disable depth writes
	if (entalpha < 1)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
	}

	GL_UseProgram (r_world_program);

// Bind the buffers
	GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, gl_bmodel_ebo);

	GL_EnableVertexAttribArrayFunc (vertAttrIndex);
	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (LMCoordsAttrIndex);

	GL_VertexAttribPointerFunc (vertAttrIndex,      3, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0));
	GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 3);
	GL_VertexAttribPointerFunc (LMCoordsAttrIndex,  2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float *)0) + 5);

// set uniforms
	GL_Uniform1iFunc (texLoc, 0);
	GL_Uniform1iFunc (LMTexLoc, 1);
	GL_Uniform1iFunc (fullbrightTexLoc, 2);
	GL_Uniform1iFunc (useFullbrightTexLoc, 0);
	GL_Uniform1iFunc (useOverbrightLoc, overbright);
	GL_Uniform1iFunc (useAlphaTestLoc, 0);
	GL_Uniform1iFunc (useLightmapWideLoc, wide10bits);
	GL_Uniform1iFunc (useLightmapOnlyLoc, 0);
	GL_Uniform1fFunc (alphaLoc, entalpha);

	for (i=0 ; i<model->numtextures ; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTURB | SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

	// Enable/disable TMU 2 (fullbrights)
	// FIXME: Move below to where we bind GL_TEXTURE0
		if (gl_fullbrights.value && (fullbright = R_TextureAnimation(t, ent != NULL ? ent->frame : 0)->fullbright))
		{
			GL_SelectTexture (GL_TEXTURE2);
			GL_Bind (fullbright);
			GL_Uniform1iFunc (useFullbrightTexLoc, 1);
		}
		else
			GL_Uniform1iFunc (useFullbrightTexLoc, 0);

		R_ClearBatch ();

		bound = false;
		lastlightmap = 0; // avoid compiler warning
		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!bound) //only bind once we are sure we need this texture
			{
				GL_SelectTexture (GL_TEXTURE0);
				GL_Bind ((R_TextureAnimation(t, ent != NULL ? ent->frame : 0))->gltexture);
				if (t->texturechains[chain]->flags & SURF_DRAWFENCE)
					GL_Uniform1iFunc (useAlphaTestLoc, 1); // Flip alpha test back on
										
				bound = true;
				lastlightmap = s->lightmaptexturenum;
			}
				
			if (s->lightmaptexturenum != lastlightmap)
				R_FlushBatch ();

			GL_SelectTexture (GL_TEXTURE1);
			GL_Bind (lightmaps[s->lightmaptexturenum].texture);
			lastlightmap = s->lightmaptexturenum;
			R_BatchSurface (s);

			rs_brushpasses++;
		}

		R_FlushBatch ();

		if (bound && t->texturechains[chain]->flags & SURF_DRAWFENCE)
			GL_Uniform1iFunc (useAlphaTestLoc, 0); // Flip alpha test back off
	}

	// clean up
	GL_DisableVertexAttribArrayFunc (vertAttrIndex);
	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (LMCoordsAttrIndex);

	GL_UseProgram (0);
	GL_SelectTexture (GL_TEXTURE0);

	if (entalpha < 1)
	{
		glDepthMask (GL_TRUE);
		glDisable (GL_BLEND);
	}
}

/*
================
R_DrawLightmapChains_GLSL -- ericw
================
*/
void R_DrawLightmapChains_GLSL(qmodel_t* model, entity_t* ent, texchain_t chain)
{
	const int	overbright = !!gl_overbright.value;
	const int	wide10bits = !!r_lightmapwide.value;

	int			i;
	msurface_t* s;
	texture_t* t;
	int		lastlightmap;

	GL_UseProgram(r_world_program);

	// Bind the buffers
	GL_BindBuffer(GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BindBuffer(GL_ELEMENT_ARRAY_BUFFER, gl_bmodel_ebo);

	GL_EnableVertexAttribArrayFunc(vertAttrIndex);
	GL_EnableVertexAttribArrayFunc(texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc(LMCoordsAttrIndex);

	GL_VertexAttribPointerFunc(vertAttrIndex, 3, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float*)0));
	GL_VertexAttribPointerFunc(texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float*)0) + 3);
	GL_VertexAttribPointerFunc(LMCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof(float), ((float*)0) + 5);

	// set uniforms
	GL_Uniform1iFunc(texLoc, 0);
	GL_Uniform1iFunc(LMTexLoc, 1);
	GL_Uniform1iFunc(fullbrightTexLoc, 2);
	GL_Uniform1iFunc(useFullbrightTexLoc, 0);
	GL_Uniform1iFunc(useOverbrightLoc, overbright);
	GL_Uniform1iFunc(useAlphaTestLoc, 0);
	GL_Uniform1iFunc(useLightmapWideLoc, wide10bits);
	GL_Uniform1fFunc(alphaLoc, 1.0f);
	GL_Uniform1iFunc(useFullbrightTexLoc, 0);
	GL_Uniform1iFunc(useLightmapOnlyLoc, 1);

	R_ClearBatch();
	lastlightmap = -1;

	for (i = 0; i < model->numtextures; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

		if (t->texturechains[chain]->texinfo->flags & TEX_SPECIAL)
			continue; // unlit water

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (s->lightmaptexturenum < 0)
				continue;

			if (s->lightmaptexturenum != lastlightmap)
			{
				R_FlushBatch();

				GL_SelectTexture(GL_TEXTURE1);
				GL_Bind(lightmaps[s->lightmaptexturenum].texture);
				lastlightmap = s->lightmaptexturenum;
			}
			R_BatchSurface(s);

			rs_brushpasses++;
		}
	}

	R_FlushBatch();

	// clean up
	GL_DisableVertexAttribArrayFunc(vertAttrIndex);
	GL_DisableVertexAttribArrayFunc(texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc(LMCoordsAttrIndex);

	GL_UseProgram(0);
	GL_SelectTexture(GL_TEXTURE0);
}

/*
=============
R_DrawWorld -- johnfitz -- rewritten
=============
*/
void R_DrawTextureChains (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	float entalpha;
	
	if (ent != NULL)
		entalpha = ENTALPHA_DECODE(ent->alpha);
	else
		entalpha = 1;

	R_UploadLightmaps ();

	if (r_drawflat_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		R_DrawTextureChains_Drawflat (model, chain);
		glEnable (GL_TEXTURE_2D);
		return;
	}

	if (r_fullbright_cheatsafe)
	{
		R_BeginTransparentDrawing (entalpha);
		R_DrawTextureChains_TextureOnly (model, ent, chain);
		R_EndTransparentDrawing (entalpha);
		goto fullbrights;
	}

	if (r_lightmap_cheatsafe)
	{
		if (r_world_program != 0)
		{
			R_DrawLightmapChains_GLSL(model, ent, chain);
			return;
		}

		if (!gl_overbright.value)
		{
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			glColor3f(0.5, 0.5, 0.5);
		}
		R_DrawLightmapChains ();
		if (!gl_overbright.value)
		{
			glColor3f(1,1,1);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		R_DrawTextureChains_White (model, chain);
		return;
	}

	R_BeginTransparentDrawing (entalpha);

	R_DrawTextureChains_NoTexture (model, chain);

	// OpenGL 2 fast path
	if (r_world_program != 0)
	{
		R_EndTransparentDrawing (entalpha);
		
		R_DrawTextureChains_GLSL (model, ent, chain);
		return;
	}

	if (gl_overbright.value)
	{
		if (gl_texture_env_combine && gl_mtexable) //case 1: texture and lightmap in one pass, overbright using texture combiners
		{
			GL_EnableMultitexture ();
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_PREVIOUS_EXT);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_TEXTURE);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			GL_DisableMultitexture ();
			R_DrawTextureChains_Multitexture (model, ent, chain);
			GL_EnableMultitexture ();
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DisableMultitexture ();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else if (entalpha < 1) //case 2: can't do multipass if entity has alpha, so just draw the texture
		{
			R_DrawTextureChains_TextureOnly (model, ent, chain);
		}
		else //case 3: texture in one pass, lightmap in second pass using 2x modulation blend func, fog in third pass
		{
			//to make fog work with multipass lightmapping, need to do one pass
			//with no fog, one modulate pass with black fog, and one additive
			//pass with black geometry and normal fog
			Fog_DisableGFog ();
			R_DrawTextureChains_TextureOnly (model, ent, chain);
			Fog_EnableGFog ();
			glDepthMask (GL_FALSE);
			glEnable (GL_BLEND);
			glBlendFunc (GL_DST_COLOR, GL_SRC_COLOR); //2x modulate
			Fog_StartAdditive ();
			R_DrawLightmapChains ();
			Fog_StopAdditive ();
			if (Fog_GetDensity() > 0)
			{
				glBlendFunc(GL_ONE, GL_ONE); //add
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glColor3f(0,0,0);
				R_DrawTextureChains_TextureOnly (model, ent, chain);
				glColor3f(1,1,1);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
			glDepthMask (GL_TRUE);
		}
	}
	else
	{
		if (gl_mtexable) //case 4: texture and lightmap in one pass, regular modulation
		{
			GL_EnableMultitexture ();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DisableMultitexture ();
			R_DrawTextureChains_Multitexture (model, ent, chain);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else if (entalpha < 1) //case 5: can't do multipass if entity has alpha, so just draw the texture
		{
			R_DrawTextureChains_TextureOnly (model, ent, chain);
		}
		else //case 6: texture in one pass, lightmap in a second pass, fog in third pass
		{
			//to make fog work with multipass lightmapping, need to do one pass
			//with no fog, one modulate pass with black fog, and one additive
			//pass with black geometry and normal fog
			Fog_DisableGFog ();
			R_DrawTextureChains_TextureOnly (model, ent, chain);
			Fog_EnableGFog ();
			glDepthMask (GL_FALSE);
			glEnable (GL_BLEND);
			glBlendFunc(GL_ZERO, GL_SRC_COLOR); //modulate
			Fog_StartAdditive ();
			R_DrawLightmapChains ();
			Fog_StopAdditive ();
			if (Fog_GetDensity() > 0)
			{
				glBlendFunc(GL_ONE, GL_ONE); //add
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				glColor3f(0,0,0);
				R_DrawTextureChains_TextureOnly (model, ent, chain);
				glColor3f(1,1,1);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
			glDepthMask (GL_TRUE);
		}
	}

	R_EndTransparentDrawing (entalpha);

fullbrights:
	if (gl_fullbrights.value)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
		glBlendFunc (GL_ONE, GL_ONE);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor3f (entalpha, entalpha, entalpha);
		Fog_StartAdditive ();
		R_DrawTextureChains_Glow (model, ent, chain);
		Fog_StopAdditive ();
		glColor3f (1, 1, 1);
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable (GL_BLEND);
		glDepthMask (GL_TRUE);
	}
}

/*
=============
R_DrawWorld -- ericw -- moved from R_DrawTextureChains, which is no longer specific to the world.
=============
*/
void R_DrawWorld (void)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_DrawTextureChains (cl.worldmodel, NULL, chain_world);
}

/*
=============
R_DrawWorld_Water -- ericw -- moved from R_DrawTextureChains_Water, which is no longer specific to the world.
=============
*/
void R_DrawWorld_Water (void)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_DrawTextureChains_Water (cl.worldmodel, NULL, chain_world);
}

/*
=============
R_DrawWorld_ShowTris -- ericw -- moved from R_DrawTextureChains_ShowTris, which is no longer specific to the world.
=============
*/
void R_DrawWorld_ShowTris (void)
{
	if (!r_drawworld_cheatsafe)
		return;

	R_DrawTextureChains_ShowTris (cl.worldmodel, chain_world);
}
