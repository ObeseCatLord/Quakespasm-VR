/*
 * Client side of the editable terrain prototype: meshes and draws the region the server owns
 * (terrain_active, see sv_terrain.c), plus the console commands for trying it.
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * Only a listen server's own client sees terrain so far. This file never edits the field: its
 * commands go through SV_Terrain_Edit, the same path a networked edit will take.
 * Lighting is computed per vertex on the CPU (ambient occlusion plus a lamp at the eye) and drawn
 * through the untextured basic shader; a terrain shader with triplanar textures and the clustered
 * dynamic lights is later work.
 */
#include "quakedef.h"
#include "terrain.h"

typedef struct
{
	float pos[3];
	float normal[3];
	byte  material;
	byte  ao;
	byte  up; // how much the surface faces up, 0-255; snow settles on it in the ice look
} terrain_drawvert_t;

typedef struct
{
	terrain_drawvert_t *verts;
	int					num_verts, cap_verts;
	vec3_t				mins, maxs;
} terrain_draw_t;

static terrain_t	  *terrain; // the region the draws below were built for; equals terrain_active once R_Terrain_Update ran
static unsigned		   terrain_draw_epoch;
static int			   terrain_draw_count;
static terrain_draw_t *terrain_draws;
static terrain_mesh_t  terrain_scratch;
static double		   terrain_last_remesh_ms;
static int			   terrain_last_remesh_chunks;

static cvar_t r_terrain = {"r_terrain", "1", CVAR_NONE};
static cvar_t r_terrain_lamp = {"r_terrain_lamp", "450", CVAR_NONE};		// reach of the eye lamp in units; 0 turns it off
static cvar_t r_terrain_biome = {"r_terrain_biome", "1", CVAR_NONE};		// 0 rock cave, 1 ice cave
static cvar_t r_terrain_ambient = {"r_terrain_ambient", "0.16", CVAR_NONE};
static cvar_t r_terrain_dlights = {"r_terrain_dlights", "1.4", CVAR_NONE}; // strength of rockets, explosions, flares on the terrain; 0 off

static const float terrain_colors[2][TERRAIN_MAT_COUNT][3] = {
	{
		{0.44f, 0.41f, 0.39f}, // rock
		{0.46f, 0.31f, 0.19f}, // dirt
		{0.95f, 0.72f, 0.18f}, // gold
		{0.62f, 0.64f, 0.60f}, // player fill
	},
	{
		{0.50f, 0.74f, 0.93f}, // glacier ice
		{0.90f, 0.94f, 1.00f}, // packed snow
		{0.16f, 0.34f, 0.66f}, // old blue ice
		{0.80f, 0.90f, 0.98f}, // cut ice
	},
};
static const float terrain_snow[3] = {0.94f, 0.97f, 1.0f};
static const float terrain_deep_ice[3] = {0.05f, 0.22f, 0.45f}; // what shadowed ice falls to instead of black

static void Terrain_FreeDraws (void)
{
	for (int i = 0; i < terrain_draw_count; ++i)
		free (terrain_draws[i].verts);
	free (terrain_draws);
	terrain_draws = NULL;
	terrain_draw_count = 0;
	terrain = NULL;
}

// Expands a chunk's indexed mesh to unshared triangles: rock keeps one normal per face so it
// reads as facets, dirt keeps the smooth field normals.
static void Terrain_BuildDraw (int index, const terrain_mesh_t *mesh)
{
	terrain_draw_t *draw = &terrain_draws[index];
	draw->num_verts = mesh->num_indices;
	if (draw->num_verts > draw->cap_verts)
	{
		draw->cap_verts = draw->num_verts + draw->num_verts / 4;
		draw->verts = (terrain_drawvert_t *)realloc (draw->verts, draw->cap_verts * sizeof (*draw->verts));
	}
	for (int k = 0; k < mesh->num_indices; k += 3)
	{
		const terrain_vertex_t *v[3] = {&mesh->verts[mesh->indices[k]], &mesh->verts[mesh->indices[k + 1]], &mesh->verts[mesh->indices[k + 2]]};
		vec3_t					e1, e2, face, sum;
		VectorSubtract (v[1]->pos, v[0]->pos, e1);
		VectorSubtract (v[2]->pos, v[0]->pos, e2);
		CrossProduct (e1, e2, face);
		VectorNormalize (face);
		VectorAdd (v[0]->normal, v[1]->normal, sum);
		VectorAdd (sum, v[2]->normal, sum);
		if (DotProduct (face, sum) < 0.0f)
			VectorInverse (face);
		const qboolean smooth = v[0]->material == TERRAIN_MAT_DIRT && v[1]->material == TERRAIN_MAT_DIRT && v[2]->material == TERRAIN_MAT_DIRT;
		for (int i = 0; i < 3; ++i)
		{
			terrain_drawvert_t *out = &draw->verts[k + i];
			VectorCopy (v[i]->pos, out->pos);
			VectorCopy (smooth ? v[i]->normal : face, out->normal);
			out->material = v[i]->material < TERRAIN_MAT_COUNT ? v[i]->material : 0;
			out->ao = v[i]->ao;
			out->up = (byte)(CLAMP (0.0f, v[i]->normal[2], 1.0f) * 255.0f);
		}
	}
}

static void Terrain_RemeshDirty (void)
{
	const double start = Sys_DoubleTime ();
	int			 remeshed = 0;
	for (int cz = 0; cz < terrain->size[2]; ++cz)
		for (int cy = 0; cy < terrain->size[1]; ++cy)
			for (int cx = 0; cx < terrain->size[0]; ++cx)
			{
				terrain_chunk_t *chunk = Terrain_Chunk (terrain, cx, cy, cz);
				if (!chunk->dirty)
					continue;
				chunk->dirty = 0;
				Terrain_MeshChunk (terrain, cx, cy, cz, &terrain_scratch);
				Terrain_BuildDraw ((cz * terrain->size[1] + cy) * terrain->size[0] + cx, &terrain_scratch);
				++remeshed;
			}
	terrain_last_remesh_ms = (Sys_DoubleTime () - start) * 1000.0;
	terrain_last_remesh_chunks = remeshed;
}

/*
Main thread, once per frame before drawing: follow the server's region and remesh what changed.
*/
void R_Terrain_Update (void)
{
	if (terrain_draw_epoch != terrain_epoch)
	{
		terrain_draw_epoch = terrain_epoch;
		Terrain_FreeDraws ();
		terrain = terrain_active;
		if (terrain)
		{
			terrain_draw_count = terrain->size[0] * terrain->size[1] * terrain->size[2];
			terrain_draws = (terrain_draw_t *)calloc (terrain_draw_count, sizeof (*terrain_draws));
			for (int i = 0; i < terrain_draw_count; ++i)
			{
				const int cell[3] = {i % terrain->size[0], i / terrain->size[0] % terrain->size[1], i / (terrain->size[0] * terrain->size[1])};
				for (int a = 0; a < 3; ++a)
				{
					terrain_draws[i].mins[a] = (float)(terrain->origin[a] + cell[a] * TERRAIN_CHUNK_UNITS - TERRAIN_CELL);
					terrain_draws[i].maxs[a] = (float)(terrain->origin[a] + (cell[a] + 1) * TERRAIN_CHUNK_UNITS + TERRAIN_CELL);
				}
			}
		}
	}
	if (!terrain)
		return;
	Terrain_RemeshDirty ();
	if (terrain_last_remesh_chunks > 8)
	{
		int tris = 0;
		for (int i = 0; i < terrain_draw_count; ++i)
			tris += terrain_draws[i].num_verts / 3;
		Con_Printf ("terrain: meshed %d chunks in %.1f ms, %d triangles\n", terrain_last_remesh_chunks, terrain_last_remesh_ms, tris);
	}
	else if (terrain_last_remesh_chunks)
		Con_DPrintf ("terrain: remesh %.3f ms over %d chunks\n", terrain_last_remesh_ms, terrain_last_remesh_chunks);
}

/*
terrain_spawn [seed] [chunks_x chunks_y chunks_z]
Creates a cave region centred on the view position; its start room surrounds the player.
*/
static void Terrain_Spawn_f (void)
{
	if (!sv.active)
	{
		Con_Printf ("terrain_spawn: needs a local server\n");
		return;
	}
	const uint32_t seed = Cmd_Argc () > 1 ? (uint32_t)atoi (Cmd_Argv (1)) : 1;
	int32_t		   size[3] = {4, 4, 3};
	if (Cmd_Argc () > 4)
		for (int i = 0; i < 3; ++i)
			size[i] = CLAMP (1, atoi (Cmd_Argv (2 + i)), 16);
	SV_Terrain_SpawnAt (r_refdef.vieworg, seed, size);
}

/*
terrain_probe
Prints where the local player is relative to the terrain; the scripted collision check reads it.
*/
static void Terrain_Probe_f (void)
{
	if (!terrain_active || !sv.active || !sv_player)
	{
		Con_Printf ("terrain_probe: needs a terrain and a local server\n");
		return;
	}
	const terrain_op_t *ops;
	const float *o = sv_player->v.origin;
	const vec3_t center = {o[0], o[1], o[2] + 4.0f};
	Con_Printf (
		"terrain_probe: origin %.1f %.1f %.1f vel %.0f %.0f %.0f onground %d field %.1f overlap %d ops %d\n", o[0], o[1], o[2], sv_player->v.velocity[0],
		sv_player->v.velocity[1], sv_player->v.velocity[2], ((int)sv_player->v.flags & FL_ONGROUND) != 0, Terrain_SampleWorld (terrain_active, center),
		Terrain_BoxOverlaps (terrain_active, o, sv_player->v.mins, sv_player->v.maxs), SV_Terrain_OpLog (&ops));
}

// Marches the view ray through the field. Returns false when nothing is hit within range.
static qboolean Terrain_ViewTrace (vec3_t hit)
{
	vec3_t forward, right, up;
	AngleVectors (cl.viewangles, forward, right, up);
	float travelled = 0.0f;
	while (travelled < 4096.0f)
	{
		VectorMA (r_refdef.vieworg, travelled, forward, hit);
		const float d = Terrain_SampleWorld (terrain_active, hit);
		if (d <= 1.0f)
			return true; // also true at once when the eye is inside solid, so a buried player can dig out
		travelled += q_max (d * 0.8f, 2.0f);
	}
	return false;
}

/*
terrain_dig [radius]   terrain_fill [radius]
Applies one sphere op where the view ray meets the terrain. Radius is in Quake units.
*/
static void Terrain_Edit (int kind)
{
	if (!terrain_active || !sv.active)
	{
		Con_Printf ("no terrain on a local server; use terrain_spawn or a drg* map\n");
		return;
	}
	const float radius = Cmd_Argc () > 1 ? (float)atof (Cmd_Argv (1)) : 56.0f;
	vec3_t		hit;
	if (!Terrain_ViewTrace (hit))
	{
		Con_Printf ("terrain: nothing in view\n");
		return;
	}
	const int changed = SV_Terrain_Edit (kind, hit, radius);
	Con_DPrintf ("terrain: %s r=%g changed %d samples\n", kind == TERRAIN_OP_DIG ? "dig" : "fill", radius, changed);
}

static void Terrain_Dig_f (void)
{
	Terrain_Edit (TERRAIN_OP_DIG);
}

static void Terrain_Fill_f (void)
{
	Terrain_Edit (TERRAIN_OP_FILL);
}

static void Terrain_Clear_f (void)
{
	SV_Terrain_Clear ();
}

/*
terrain_bench
Remeshes every chunk a few times and reports the per-chunk cost inside the engine build.
*/
static void Terrain_Bench_f (void)
{
	if (!terrain)
	{
		Con_Printf ("no terrain; use terrain_spawn first\n");
		return;
	}
	double total = 0.0, worst = 0.0;
	int	   surface = 0;
	for (int rep = 0; rep < 4; ++rep)
		for (int cz = 0; cz < terrain->size[2]; ++cz)
			for (int cy = 0; cy < terrain->size[1]; ++cy)
				for (int cx = 0; cx < terrain->size[0]; ++cx)
				{
					const double start = Sys_DoubleTime ();
					Terrain_MeshChunk (terrain, cx, cy, cz, &terrain_scratch);
					const double ms = (Sys_DoubleTime () - start) * 1000.0;
					if (!terrain_scratch.num_indices)
						continue;
					total += ms;
					worst = q_max (worst, ms);
					++surface;
				}
	if (surface)
		Con_Printf ("terrain_bench: %d surface chunks, mean %.3f ms, worst %.3f ms per chunk remesh\n", surface / 4, total / surface, worst);
}

// The server replaces the region on map load (sv_terrain.c); R_Terrain_Update picks that up.
void R_Terrain_NewMap (void) {}

void R_Terrain_Init (void)
{
	Cvar_RegisterVariable (&r_terrain);
	Cvar_RegisterVariable (&r_terrain_lamp);
	Cvar_RegisterVariable (&r_terrain_ambient);
	Cvar_RegisterVariable (&r_terrain_biome);
	Cvar_RegisterVariable (&r_terrain_dlights);
	Cmd_AddCommand ("terrain_spawn", Terrain_Spawn_f);
	Cmd_AddCommand ("terrain_dig", Terrain_Dig_f);
	Cmd_AddCommand ("terrain_fill", Terrain_Fill_f);
	Cmd_AddCommand ("terrain_clear", Terrain_Clear_f);
	Cmd_AddCommand ("terrain_bench", Terrain_Bench_f);
	Cmd_AddCommand ("terrain_probe", Terrain_Probe_f);
}

void R_DrawTerrain (cb_context_t *cbx)
{
	if (!terrain_draw_count || !r_terrain.value)
		return;

	R_BeginDebugUtilsLabel (cbx, "terrain");
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.terrain_pipeline[cbx->pipeline_variant]);

	const float lamp = r_terrain_lamp.value;
	const float lamp_falloff = lamp > 0.0f ? 1.0f / (lamp * lamp * 0.08f) : 0.0f; // half brightness at about 0.28 of the reach
	const float ambient = r_terrain_ambient.value;
	const int	ice = r_terrain_biome.value != 0.0f;

	// The engine's dynamic lights (rocket glow, explosions, muzzle flashes, flares). Plain white
	// ones are warmed so fire reads against the ice.
	const dlight_t *lights[MAX_DLIGHTS];
	float			light_rgb[MAX_DLIGHTS][3];
	int				num_lights = 0;
	if (r_terrain_dlights.value > 0.0f)
		for (int i = 0; i < MAX_DLIGHTS; ++i)
		{
			const dlight_t *dl = &cl_dlights[i];
			if (dl->die < cl.time || dl->radius <= 0.0f)
				continue;
			const qboolean white = fabsf (dl->color[0] - dl->color[1]) < 0.05f && fabsf (dl->color[1] - dl->color[2]) < 0.05f;
			const float	   level = white ? (dl->color[0] > 0.0f ? dl->color[0] : 1.0f) : 1.0f;
			light_rgb[num_lights][0] = (white ? level : dl->color[0]) * r_terrain_dlights.value;
			light_rgb[num_lights][1] = (white ? level * 0.74f : dl->color[1]) * r_terrain_dlights.value;
			light_rgb[num_lights][2] = (white ? level * 0.42f : dl->color[2]) * r_terrain_dlights.value;
			lights[num_lights++] = dl;
		}
	for (int i = 0; i < terrain_draw_count; ++i)
	{
		const terrain_draw_t *draw = &terrain_draws[i];
		if (!draw->num_verts || R_CullBox ((float *)draw->mins, (float *)draw->maxs))
			continue;

		int near_lights[MAX_DLIGHTS], num_near = 0;
		for (int l = 0; l < num_lights; ++l)
		{
			float gap2 = 0.0f;
			for (int a = 0; a < 3; ++a)
			{
				const float o = lights[l]->origin[a];
				const float d = o < draw->mins[a] ? draw->mins[a] - o : o > draw->maxs[a] ? o - draw->maxs[a] : 0.0f;
				gap2 += d * d;
			}
			if (gap2 < lights[l]->radius * lights[l]->radius)
				near_lights[num_near++] = l;
		}

		VkBuffer	   buffer;
		VkDeviceSize   buffer_offset;
		basicvertex_t *out = (basicvertex_t *)R_VertexAllocate (draw->num_verts * sizeof (basicvertex_t), &buffer, &buffer_offset);
		for (int k = 0; k < draw->num_verts; ++k)
		{
			const terrain_drawvert_t *v = &draw->verts[k];
			const float				  ao = 0.25f + 0.75f * (float)v->ao / 255.0f;
			// sky-ish hemisphere term so floors, walls and ceilings separate even without the lamp
			float light = ambient * (0.75f + 0.25f * v->normal[2]);
			float shine = 0.0f;
			if (lamp > 0.0f)
			{
				vec3_t to_eye;
				VectorSubtract (r_origin, v->pos, to_eye);
				const float dist2 = DotProduct (to_eye, to_eye);
				const float facing = DotProduct (to_eye, v->normal) / sqrtf (dist2 + 1.0f);
				if (facing > 0.0f)
				{
					const float reach = 1.0f / (1.0f + dist2 * lamp_falloff);
					light += 0.85f * facing * reach;
					if (ice)
					{
						const float f4 = facing * facing * facing * facing;
						shine = 0.55f * f4 * f4 * f4 * reach; // tight glint where the ice faces the lamp
					}
				}
			}
			float rgb[3];
			VectorCopy (terrain_colors[ice][v->material], rgb);
			if (ice)
			{
				// snow settles on what faces up; shadowed ice goes deep blue instead of black
				const float snow = CLAMP (0.0f, ((float)v->up / 255.0f - 0.55f) * 3.0f, 1.0f);
				for (int c = 0; c < 3; ++c)
				{
					rgb[c] += (terrain_snow[c] - rgb[c]) * snow;
					rgb[c] = terrain_deep_ice[c] * (1.0f - ao) * (1.0f - snow) + rgb[c] * (snow + (1.0f - snow) * ao);
				}
				light = q_min (light, 1.0f);
			}
			else
				light = q_min (light * ao, 1.0f);
			float lit[3] = {light, light, light};
			for (int n = 0; n < num_near; ++n)
			{
				const dlight_t *dl = lights[near_lights[n]];
				vec3_t			to_light;
				VectorSubtract (dl->origin, v->pos, to_light);
				const float dist2 = DotProduct (to_light, to_light);
				if (dist2 >= dl->radius * dl->radius)
					continue;
				const float dist = sqrtf (dist2);
				float		fall = 1.0f - dist / dl->radius;
				// ice passes some light through, so it wraps further round than rock does
				const float wrap = ice ? 0.45f : 0.1f;
				const float facing = (DotProduct (to_light, v->normal) / (dist + 1.0f) + wrap) / (1.0f + wrap);
				if (facing <= 0.0f)
					continue;
				fall *= fall * facing * (ice ? 1.0f : ao);
				for (int c = 0; c < 3; ++c)
					lit[c] += light_rgb[near_lights[n]][c] * fall;
			}
			VectorCopy (v->pos, out[k].position);
			out[k].texcoord[0] = out[k].texcoord[1] = 0.0f;
			for (int c = 0; c < 3; ++c)
				out[k].color[c] = (byte)(q_min (rgb[c] * lit[c] + shine, 1.0f) * 255.0f);
			out[k].color[3] = 255;
		}
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
		vulkan_globals.vk_cmd_draw (cbx->cb, draw->num_verts, 1, 0, 0);
	}
	R_EndDebugUtilsLabel (cbx);
}
