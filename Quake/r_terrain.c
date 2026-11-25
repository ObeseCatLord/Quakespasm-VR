/*
 * Client side of the editable terrain prototype: owns one terrain region,
 * the console commands that create and edit it, and its draw.
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * Round 1 scope: no collision, no networking. Lighting is computed per vertex
 * on the CPU (ambient occlusion plus a lamp at the eye) and drawn through the
 * untextured basic shader; a terrain shader with triplanar textures and the
 * clustered dynamic lights is later work.
 */
#include "quakedef.h"
#include "terrain.h"

typedef struct
{
	float pos[3];
	float normal[3];
	byte  rgb[3];
	byte  ao;
} terrain_drawvert_t;

typedef struct
{
	terrain_drawvert_t *verts;
	int					num_verts, cap_verts;
	vec3_t				mins, maxs;
} terrain_draw_t;

static terrain_t	  *terrain;
static terrain_draw_t *terrain_draws;
static terrain_mesh_t  terrain_scratch;
static double		   terrain_last_remesh_ms;
static int			   terrain_last_remesh_chunks;

static cvar_t r_terrain = {"r_terrain", "1", CVAR_NONE};
static cvar_t r_terrain_lamp = {"r_terrain_lamp", "700", CVAR_ARCHIVE};		// reach of the eye lamp in units; 0 turns it off
static cvar_t r_terrain_ambient = {"r_terrain_ambient", "0.12", CVAR_ARCHIVE};

static const float terrain_colors[TERRAIN_MAT_COUNT][3] = {
	{0.44f, 0.41f, 0.39f}, // rock
	{0.46f, 0.31f, 0.19f}, // dirt
	{0.95f, 0.72f, 0.18f}, // gold
	{0.62f, 0.64f, 0.60f}, // player fill
};

static void Terrain_Clear (void)
{
	if (!terrain)
		return;
	const int count = terrain->size[0] * terrain->size[1] * terrain->size[2];
	for (int i = 0; i < count; ++i)
		free (terrain_draws[i].verts);
	free (terrain_draws);
	terrain_draws = NULL;
	Terrain_Free (terrain);
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
			const float		   *color = terrain_colors[v[i]->material < TERRAIN_MAT_COUNT ? v[i]->material : 0];
			VectorCopy (v[i]->pos, out->pos);
			VectorCopy (smooth ? v[i]->normal : face, out->normal);
			for (int c = 0; c < 3; ++c)
				out->rgb[c] = (byte)(color[c] * 255.0f);
			out->ao = v[i]->ao;
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
terrain_spawn [seed] [chunks_x chunks_y chunks_z]
Creates a cave region centred on the view position; its start room surrounds the player.
*/
static void Terrain_Spawn_f (void)
{
	const uint32_t seed = Cmd_Argc () > 1 ? (uint32_t)atoi (Cmd_Argv (1)) : 1;
	int32_t		   size[3] = {4, 4, 3}, origin[3];
	if (Cmd_Argc () > 4)
		for (int i = 0; i < 3; ++i)
			size[i] = CLAMP (1, atoi (Cmd_Argv (2 + i)), 16);
	Terrain_Clear ();
	for (int i = 0; i < 3; ++i)
	{
		const int center = (int)floorf (r_refdef.vieworg[i] / TERRAIN_CELL + 0.5f) * TERRAIN_CELL;
		origin[i] = center - size[i] * TERRAIN_CHUNK_UNITS / 2;
	}
	terrain = Terrain_Create (origin, size, seed);
	const int count = size[0] * size[1] * size[2];
	terrain_draws = (terrain_draw_t *)calloc (count, sizeof (*terrain_draws));
	for (int i = 0; i < count; ++i)
	{
		const int cell[3] = {i % size[0], i / size[0] % size[1], i / (size[0] * size[1])};
		for (int a = 0; a < 3; ++a)
		{
			terrain_draws[i].mins[a] = (float)(origin[a] + cell[a] * TERRAIN_CHUNK_UNITS - TERRAIN_CELL);
			terrain_draws[i].maxs[a] = (float)(origin[a] + (cell[a] + 1) * TERRAIN_CHUNK_UNITS + TERRAIN_CELL);
		}
	}
	const double start = Sys_DoubleTime ();
	Terrain_Generate (terrain);
	const double generated = Sys_DoubleTime ();
	Terrain_RemeshDirty ();
	int tris = 0;
	for (int i = 0; i < count; ++i)
		tris += terrain_draws[i].num_verts / 3;
	Con_Printf (
		"terrain: seed %u, %dx%dx%d chunks, %d triangles; generate %.1f ms, mesh %.1f ms (%d chunks), hash %016llx\n", seed, size[0], size[1], size[2], tris,
		(generated - start) * 1000.0, terrain_last_remesh_ms, terrain_last_remesh_chunks, (unsigned long long)Terrain_Hash (terrain));
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
		const float d = Terrain_SampleWorld (terrain, hit);
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
	if (!terrain)
	{
		Con_Printf ("no terrain; use terrain_spawn first\n");
		return;
	}
	const float radius = Cmd_Argc () > 1 ? CLAMP (8.0f, (float)atof (Cmd_Argv (1)), 512.0f) : 56.0f;
	vec3_t		hit;
	if (!Terrain_ViewTrace (hit))
	{
		Con_Printf ("terrain: nothing in view\n");
		return;
	}
	// The op is quantized here; the quantized op is what a server would send and every peer replay.
	terrain_op_t op;
	op.kind = (uint8_t)kind;
	op.material = TERRAIN_MAT_FILL;
	op.radius = (uint16_t)(radius * TERRAIN_SUB / TERRAIN_CELL);
	for (int i = 0; i < 3; ++i)
		op.center[i] = (int32_t)floorf ((hit[i] - (float)terrain->origin[i]) * TERRAIN_SUB / TERRAIN_CELL + 0.5f);
	const double start = Sys_DoubleTime ();
	const int	 changed = Terrain_ApplyOp (terrain, &op);
	const double applied = Sys_DoubleTime ();
	Terrain_RemeshDirty ();
	Con_DPrintf (
		"terrain: %s r=%g changed %d samples; op %.3f ms, remesh %.3f ms over %d chunks\n", kind == TERRAIN_OP_DIG ? "dig" : "fill", radius, changed,
		(applied - start) * 1000.0, terrain_last_remesh_ms, terrain_last_remesh_chunks);
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
	Terrain_Clear ();
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

void R_Terrain_NewMap (void)
{
	Terrain_Clear ();
}

void R_Terrain_Init (void)
{
	Cvar_RegisterVariable (&r_terrain);
	Cvar_RegisterVariable (&r_terrain_lamp);
	Cvar_RegisterVariable (&r_terrain_ambient);
	Cmd_AddCommand ("terrain_spawn", Terrain_Spawn_f);
	Cmd_AddCommand ("terrain_dig", Terrain_Dig_f);
	Cmd_AddCommand ("terrain_fill", Terrain_Fill_f);
	Cmd_AddCommand ("terrain_clear", Terrain_Clear_f);
	Cmd_AddCommand ("terrain_bench", Terrain_Bench_f);
}

void R_DrawTerrain (cb_context_t *cbx)
{
	if (!terrain || !r_terrain.value)
		return;

	R_BeginDebugUtilsLabel (cbx, "terrain");
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.terrain_pipeline[cbx->pipeline_variant]);

	const float lamp = r_terrain_lamp.value;
	const float lamp_falloff = lamp > 0.0f ? 1.0f / (lamp * lamp * 0.08f) : 0.0f; // half brightness at about 0.28 of the reach
	const float ambient = r_terrain_ambient.value;
	const int	count = terrain->size[0] * terrain->size[1] * terrain->size[2];
	for (int i = 0; i < count; ++i)
	{
		const terrain_draw_t *draw = &terrain_draws[i];
		if (!draw->num_verts || R_CullBox ((float *)draw->mins, (float *)draw->maxs))
			continue;

		VkBuffer	   buffer;
		VkDeviceSize   buffer_offset;
		basicvertex_t *out = (basicvertex_t *)R_VertexAllocate (draw->num_verts * sizeof (basicvertex_t), &buffer, &buffer_offset);
		for (int k = 0; k < draw->num_verts; ++k)
		{
			const terrain_drawvert_t *v = &draw->verts[k];
			const float				  ao = 0.25f + 0.75f * (float)v->ao / 255.0f;
			// sky-ish hemisphere term so floors, walls and ceilings separate even without the lamp
			float light = ambient * (0.75f + 0.25f * v->normal[2]);
			if (lamp > 0.0f)
			{
				vec3_t to_eye;
				VectorSubtract (r_origin, v->pos, to_eye);
				const float dist2 = DotProduct (to_eye, to_eye);
				const float facing = DotProduct (to_eye, v->normal) / sqrtf (dist2 + 1.0f);
				if (facing > 0.0f)
					light += 0.85f * facing / (1.0f + dist2 * lamp_falloff);
			}
			light = q_min (light * ao, 1.0f);
			VectorCopy (v->pos, out[k].position);
			out[k].texcoord[0] = out[k].texcoord[1] = 0.0f;
			out[k].color[0] = (byte)(v->rgb[0] * light);
			out[k].color[1] = (byte)(v->rgb[1] * light);
			out[k].color[2] = (byte)(v->rgb[2] * light);
			out[k].color[3] = 255;
		}
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
		vulkan_globals.vk_cmd_draw (cbx->cb, draw->num_verts, 1, 0, 0);
	}
	R_EndDebugUtilsLabel (cbx);
}
