/*
 * Client side of the particle lava (sv_terrain_lava.c): hot glowing blobs and a few dynamic lights.
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * Reads the server's particle arrays directly (listen server only, like the props and the worm).
 * Each particle is a small fullbright octahedron drawn through the terrain pipeline, sized to
 * overlap its neighbours so a stream reads as one glowing mass rather than dots. The pipeline has
 * no additive blend, so the glow is the colour ramp (white-yellow to dark red) plus the lights.
 */
#include "quakedef.h"
#include "terrain.h"
#include "r_terrain_style.h"

#define LAVA_LIGHT_KEY 0x7e780000

void R_TerrainLava_Lights (void)
{
	const terrain_lava_view_t *v = SV_Terrain_LavaView ();
	if (!v)
		return;
	for (int i = 0; i < v->num_lights; ++i)
	{
		dlight_t *dl = CL_AllocDlight (LAVA_LIGHT_KEY + i);
		VectorCopy (v->lights[i], dl->origin);
		dl->radius = v->lights[i][3] * (0.94f + 0.06f * sinf ((float)cl.time * 11.0f + (float)i * 1.7f));
		dl->die = cl.time + 0.1;
		dl->color[0] = 0.45f;
		dl->color[1] = 0.14f;
		dl->color[2] = 0.02f;
	}
}

static void HeatColor (float t, byte rgb[3])
{
	static const float stops[5][4] = {{0.0f, 10, 2, 1}, {0.15f, 34, 5, 1}, {0.45f, 78, 16, 2}, {0.8f, 112, 52, 9}, {1.0f, 135, 105, 52}};
	t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
	int k = 0;
	while (k < 3 && t > stops[k + 1][0])
		++k;
	const float f = (t - stops[k][0]) / (stops[k + 1][0] - stops[k][0]);
	for (int a = 0; a < 3; ++a)
		rgb[a] = (byte)(stops[k][1 + a] + (stops[k + 1][1 + a] - stops[k][1 + a]) * f);
}

static int PushBlob (terrain_gpuvert_t *out, const float c[3], float r, const byte rgb[3])
{
	static const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	static const int   faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
	int				   n = 0;
	float			   face_n[8][3];
	for (int f = 0; f < 8; ++f)
		for (int a = 0; a < 3; ++a)
			face_n[f][a] = (dirs[faces[f][0]][a] + dirs[faces[f][1]][a] + dirs[faces[f][2]][a]) * 0.577f;
	for (int f = 0; f < 8; ++f)
		for (int k = 0; k < 3; ++k)
		{
			terrain_gpuvert_t *v = &out[n++];
			const float		  *d = dirs[faces[f][k]];
			const float		   shade = 0.55f + 0.45f * (0.3f * face_n[f][0] + 0.2f * face_n[f][1] + 0.93f * face_n[f][2]); // light from above
			for (int a = 0; a < 3; ++a)
				v->pos[a] = c[a] + d[a] * r;
			v->light[0] = (byte)(rgb[0] * shade);
			v->light[1] = (byte)(rgb[1] * shade);
			v->light[2] = (byte)(rgb[2] * shade);
			v->light[3] = 0;
			v->params[0] = 2;
			v->params[1] = 0;
			v->params[2] = TERRAIN_GPU_FULLBRIGHT;
			v->params[3] = 255;
		}
	return n;
}

// With the terrain pipeline already bound (called after the props).
void R_TerrainLava_Draw (cb_context_t *cbx)
{
	const terrain_lava_view_t *v = SV_Terrain_LavaView ();
	if (!v)
		return;
	VkBuffer		   buffer;
	VkDeviceSize	   buffer_offset;
	terrain_gpuvert_t *out = (terrain_gpuvert_t *)R_VertexAllocate (v->count * 24 * sizeof (terrain_gpuvert_t), &buffer, &buffer_offset);
	int				   n = 0;
	const float		   far2 = 3000.0f * 3000.0f;
	for (int i = 0; i < v->count; ++i)
	{
		const float c[3] = {v->pos[0][i], v->pos[1][i], v->pos[2][i]};
		float		rel[3];
		VectorSubtract (c, r_refdef.vieworg, rel);
		if (DotProduct (rel, rel) > far2 || DotProduct (rel, vpn) < -v->radius)
			continue;
		byte rgb[3];
		HeatColor (v->heat[i], rgb);
		n += PushBlob (out + n, c, v->radius, rgb);
	}
	if (!n)
		return;
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	vulkan_globals.vk_cmd_draw (cbx->cb, n, 1, 0, 0);
}
