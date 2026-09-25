#include "r_vrik.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

qboolean R_VRIKRefreshCachedSurfacesForTest (r_vrik_skincache_t *cache);
aliashdr_t *R_VRIKRefreshPropSurfaceForTest (r_vrik_skincache_t *cache);

/* Model allocations contain an alias header at an offset, not at the base. */
typedef struct fixture_alias_s
{
	byte prefix[80];
	aliashdr_t surface;
} fixture_alias_t;

static int header_lookups;

void *Cache_Check (cache_user_t *cache)
{
	return cache->data;
}

aliashdr_t *Mod_GetMD5Extradata (qmodel_t *model)
{
	/* The real getter reloads evicted models. Refresh must never reach it
	 * for an absent allocation while another header is borrowed. */
	assert (model && model->cache.data);
	++header_lookups;
	return &((fixture_alias_t *)model->cache.data)->surface;
}

static fixture_alias_t *Relocate (qmodel_t *model)
{
	fixture_alias_t *moved = malloc (sizeof(*moved));
	assert (moved);
	memcpy (moved, model->cache.data, sizeof(*moved));
	free (model->cache.data); /* ASan catches any read through the old header. */
	model->cache.data = moved;
	return moved;
}

int main (void)
{
	qmodel_t body = {0}, prop = {0};
	r_vrik_skincache_t cache = {0};
	md5vertex_t vertices[1] = {0};
	fixture_alias_t *bodydata = calloc (1, sizeof(*bodydata));
	fixture_alias_t *propdata = calloc (1, sizeof(*propdata));
	int lookups;

	assert (bodydata && propdata);
	body.cache.data = bodydata;
	prop.cache.data = propdata;
	cache.model = &body;
	cache.prop_model = &prop;
	cache.surface = &bodydata->surface;
	cache.prop_surface = &propdata->surface;
	cache.vertices = cache.prop_vertices = vertices;
	cache.ready = true;
	cache.hostframe = 17;
	cache.pose_generation = 3;
	cache.body_numindexes = 3;
	cache.prop_numverts = 1;
	cache.prop_numindexes = 3;
	assert (R_VRIKRefreshCachedSurfacesForTest (&cache));
	assert (cache.surface == &bodydata->surface);
	assert (cache.prop_surface == &propdata->surface);

	/* A crosshair/viewmodel load moves the weapon between stereo eyes. */
	propdata = Relocate (&prop);
	assert (R_VRIKRefreshCachedSurfacesForTest (&cache));
	assert (cache.prop_surface == &propdata->surface);
	assert (cache.surface == &bodydata->surface);
	bodydata = Relocate (&body);
	assert (R_VRIKRefreshCachedSurfacesForTest (&cache));
	assert (cache.surface == &bodydata->surface);
	assert (cache.ready && cache.hostframe == 17 && cache.pose_generation == 3);
	assert (cache.vertices == vertices && cache.prop_vertices == vertices);
	assert (cache.body_numindexes == 3 && cache.prop_numindexes == 3);

	/* Texture preparation can move the prop again within a draw. The prop
	 * draw entry points must refresh it independently of skin-cache reuse. */
	propdata = Relocate (&prop);
	assert (R_VRIKRefreshPropSurfaceForTest (&cache) == &propdata->surface);
	assert (cache.prop_surface == &propdata->surface);

	/* An evicted body makes the skin unusable without loading either model. */
	body.cache.data = NULL;
	lookups = header_lookups;
	assert (!R_VRIKRefreshCachedSurfacesForTest (&cache));
	assert (header_lookups == lookups);
	body.cache.data = bodydata;

	/* Optional equipment eviction preserves the body and clears derived prop
	 * and muzzle state without invoking the loading getter for that model. */
	free (propdata);
	prop.cache.data = NULL;
	cache.muzzle_valid = true;
	cache.muzzle_origin[0] = cache.muzzle_forward[0] = 1;
	cache.prop_semantic = MD5_VRIK_GUN;
	lookups = header_lookups;
	assert (R_VRIKRefreshCachedSurfacesForTest (&cache));
	assert (header_lookups == lookups + 1);
	assert (cache.surface == &bodydata->surface && cache.vertices == vertices);
	assert (!cache.prop_surface && !cache.prop_model);
	assert (!cache.prop_numverts && !cache.prop_numindexes);
	assert (!cache.muzzle_valid && cache.prop_semantic == -1);
	assert (!cache.muzzle_origin[0] && !cache.muzzle_forward[0]);
	assert (R_VRIKRefreshCachedSurfacesForTest (&cache));
	assert (!R_VRIKRefreshPropSurfaceForTest (&cache));
	assert (!R_VRIKRefreshPropSurfaceForTest (NULL));
	free (bodydata);
	puts ("VRIK cache: body/prop relocation, draw refresh, eviction and residency passed");
	return 0;
}
