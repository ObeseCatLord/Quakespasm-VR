/*
 * Client side of the particle lava (sv_terrain_lava.c): the drawing, the lights and the steam.
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * Reads the server's particle arrays directly (listen server only, like the props and the worm).
 * Every particle is one camera-facing quad drawn by Shaders/lava.frag through the terrain vertex
 * shader. r_terrain_lava_style picks how the quads are shaded (the shader does all the work):
 *   0 blobs   shaded spheres with a hot core and a dark rim
 *   1 goo     bigger, outline-free bodies with a dithered silhouette, so they merge into one mass
 *   2 chunky  pixel-art discs, a lava texture warping over world-locked texels, six palette steps
 *   3 crust   hot and moving is yellow-white; cooling grows black plates held apart by glowing cracks
 * Hot particles also get a faint additive glow quad. The same soft pipeline draws the steam.
 * Dynamic lights follow the hottest clusters, smoothed over time. Steam puffs are spawned from events
 * the simulation writes where hot lava meets ice, and simulated here with the frame time.
 */
#include "quakedef.h"
#include "terrain.h"
#include "terrain_lava.h"
#include "r_terrain_style.h"

#define LAVA_LIGHT_KEY	0x7e780000
#define LAVA_LIGHTS		8
#define STEAM_MAX		640
#define STYLE_COUNT		4

static const char *const style_names[STYLE_COUNT] = {"blobs", "goo", "chunky", "crust"};
static const char *const style_notes[STYLE_COUNT] = {"shaded spheres, hot core, dark rim", "big merged bodies, dithered edge", "pixel-art discs, warping texture, 6 colours",
													 "cooling plates with glowing cracks"};

static cvar_t r_terrain_lava_style = {"r_terrain_lava_style", "0", CVAR_ARCHIVE}; // 0 blobs, 1 goo, 2 chunky, 3 crust
static cvar_t r_terrain_lava_light = {"r_terrain_lava_light", "1", CVAR_ARCHIVE}; // brightness of the lava's dynamic lights; 0 turns them off
static cvar_t r_terrain_lava_steam = {"r_terrain_lava_steam", "1", CVAR_ARCHIVE}; // amount of steam where lava melts ice; 0 turns it off
static cvar_t r_terrain_lava_glow = {"r_terrain_lava_glow", "1", CVAR_ARCHIVE};	  // strength of the glow around hot particles; 0 turns it off

static int StyleCurrent (void)
{
	return CLAMP (0, (int)r_terrain_lava_style.value, STYLE_COUNT - 1);
}

static void Style_f (void)
{
	if (Cmd_Argc () >= 2)
	{
		const char *a = Cmd_Argv (1);
		int			pick = -1;
		if (!q_strcasecmp (a, "next"))
			pick = (StyleCurrent () + 1) % STYLE_COUNT;
		else if (!q_strcasecmp (a, "info"))
			pick = -2;
		else
		{
			for (int i = 0; i < STYLE_COUNT; ++i)
				if (!q_strcasecmp (a, style_names[i]))
					pick = i;
			if (pick < 0 && a[0] >= '0' && a[0] <= '9')
				pick = CLAMP (0, atoi (a), STYLE_COUNT - 1);
		}
		if (pick >= 0)
			Cvar_SetValue ("r_terrain_lava_style", (float)pick);
		else if (pick == -1)
		{
			Con_Printf ("terrain_lava_style [next | info | blobs | goo | chunky | crust]\n");
			return;
		}
		else
		{
			extern void R_TerrainLava_PrintCost (void);
			R_TerrainLava_PrintCost ();
			return;
		}
	}
	const int cur = StyleCurrent ();
	Con_Printf ("lava style %d: %s (%s)\n", cur, style_names[cur], style_notes[cur]);
}

void R_TerrainLava_Init (void)
{
	Cvar_RegisterVariable (&r_terrain_lava_style);
	Cvar_RegisterVariable (&r_terrain_lava_light);
	Cvar_RegisterVariable (&r_terrain_lava_steam);
	Cvar_RegisterVariable (&r_terrain_lava_glow);
	Cmd_AddCommand ("terrain_lava_style", Style_f);
}

/* ---- random ---- */

static unsigned rng_state = 0x1234567u;

static float Rnd (void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return (float)(rng_state & 0xffffff) / (float)0x1000000;
}

/* ---- steam ---- */

typedef struct
{
	float pos[3], vel[3];
	float age, life, size0, grow, rnd;
} puff_t;

static puff_t	puffs[STEAM_MAX];
static int		num_puffs;
static unsigned steam_seen;

static void SpawnPuffs (const terrain_lava_steam_event_t *e, float amount)
{
	float want = e->strength * 2.2f * amount;
	int	  n = (int)want;
	if (Rnd () < want - (float)n)
		++n;
	for (int k = 0; k < n; ++k)
	{
		puff_t *p;
		if (num_puffs < STEAM_MAX)
			p = &puffs[num_puffs++];
		else
			p = &puffs[(int)(Rnd () * STEAM_MAX) % STEAM_MAX]; // full: replace some other
		for (int a = 0; a < 3; ++a)
		{
			p->pos[a] = e->pos[a] + e->nrm[a] * 6.0f + (Rnd () - 0.5f) * 10.0f;
			p->vel[a] = e->nrm[a] * (10.0f + 14.0f * Rnd ()) + (Rnd () - 0.5f) * 30.0f;
		}
		p->vel[2] += 38.0f + 34.0f * Rnd ();
		p->age = 0.0f;
		p->life = 1.1f + 0.9f * Rnd ();
		p->size0 = 5.0f + 5.0f * Rnd ();
		p->grow = 14.0f + 12.0f * Rnd ();
		p->rnd = Rnd ();
	}
}

static void SteamUpdate (const terrain_lava_view2_t *v, float dt)
{
	if (!v || r_terrain_lava_steam.value <= 0.0f)
	{
		num_puffs = 0;
		if (v)
			steam_seen = v->steam_seq;
		return;
	}
	if (v->steam_seq - steam_seen > TERRAIN_LAVA_STEAM_RING)
		steam_seen = v->steam_seq - TERRAIN_LAVA_STEAM_RING;
	for (; steam_seen != v->steam_seq; ++steam_seen)
		SpawnPuffs (&v->steam[steam_seen % TERRAIN_LAVA_STEAM_RING], r_terrain_lava_steam.value);
	const float drag = expf (-0.9f * dt);
	for (int i = 0; i < num_puffs;)
	{
		puff_t *p = &puffs[i];
		p->age += dt;
		if (p->age >= p->life)
		{
			*p = puffs[--num_puffs];
			continue;
		}
		for (int a = 0; a < 3; ++a)
			p->pos[a] += p->vel[a] * dt;
		p->vel[0] *= drag;
		p->vel[1] *= drag;
		p->vel[2] = p->vel[2] * drag + 24.0f * dt; // keeps rising a little
		++i;
	}
}

/* ---- lights ---- */

typedef struct
{
	float pos[3], radius, level;
	int	  used;
} slight_t;

static slight_t slights[LAVA_LIGHTS];
static double	last_update_time;

// Once per client frame. Smooths the lights the simulation proposes (it re-picks the hottest
// clusters every few steps, so their order and sizes jump) and advances the steam.
void R_TerrainLava_Lights (void)
{
	const terrain_lava_view2_t *v = SV_Terrain_LavaView2 ();
	float						dt = (float)(cl.time - last_update_time);
	last_update_time = cl.time;
	if (dt < 0.0f || dt > 0.25f)
		dt = 0.0f;
	if (!v)
	{
		num_puffs = 0;
		for (int i = 0; i < LAVA_LIGHTS; ++i)
			slights[i].used = 0;
		return;
	}
	SteamUpdate (v, dt);

	int matched[LAVA_LIGHTS] = {0};
	for (int j = 0; j < v->num_lights; ++j)
	{
		const float *t = v->lights[j];
		int			 best = -1;
		float		 best_d = 150.0f * 150.0f;
		for (int i = 0; i < LAVA_LIGHTS; ++i)
		{
			if (!slights[i].used || matched[i])
				continue;
			float d[3];
			VectorSubtract (slights[i].pos, t, d);
			const float d2 = DotProduct (d, d);
			if (d2 < best_d)
			{
				best_d = d2;
				best = i;
			}
		}
		const float target_level = 1.0f - expf (-v->light_weight[j] / 90.0f);
		if (best < 0)
		{
			for (int i = 0; i < LAVA_LIGHTS && best < 0; ++i)
				if (!slights[i].used)
					best = i;
			if (best < 0)
				continue;
			slight_t *s = &slights[best];
			VectorCopy (t, s->pos);
			s->radius = t[3];
			s->level = 0.0f;
			s->used = 1;
		}
		matched[best] = 1;
		slight_t	*s = &slights[best];
		const float a_move = 1.0f - expf (-dt / 0.30f), a_level = 1.0f - expf (-dt / 0.45f);
		for (int a = 0; a < 3; ++a)
			s->pos[a] += (t[a] - s->pos[a]) * a_move;
		s->radius += (t[3] - s->radius) * a_move;
		s->level += (target_level - s->level) * a_level;
	}
	const float brightness = r_terrain_lava_light.value;
	for (int i = 0; i < LAVA_LIGHTS; ++i)
	{
		slight_t *s = &slights[i];
		if (!s->used)
			continue;
		if (!matched[i])
		{
			s->level -= dt / 0.6f; // a cluster that went away fades out
			if (s->level <= 0.0f)
			{
				s->used = 0;
				continue;
			}
		}
		const float level = s->level * brightness;
		if (level < 0.01f)
			continue;
		dlight_t *dl = CL_AllocDlight (LAVA_LIGHT_KEY + i);
		VectorCopy (s->pos, dl->origin);
		dl->radius = s->radius * (0.55f + 0.45f * q_min (s->level, 1.0f)) * (0.97f + 0.03f * sinf ((float)cl.time * 9.0f + (float)i * 1.7f));
		dl->die = cl.time + 0.1;
		dl->color[0] = 0.24f * level;
		dl->color[1] = 0.055f * level;
		dl->color[2] = 0.008f * level;
	}
}

/* ---- drawing ---- */

typedef struct
{
	byte kind, rnd, flags, amount;
} quad_meta_t;

static terrain_gpuvert_t *PushQuad (terrain_gpuvert_t *out, const float c[3], const float rx[3], const float ry[3], float size, byte heat, byte speed, quad_meta_t m)
{
	static const byte corner[4][2] = {{0, 0}, {255, 0}, {255, 255}, {0, 255}};
	static const int  order[6] = {0, 1, 2, 0, 2, 3};
	for (int k = 0; k < 6; ++k)
	{
		const int		   q = order[k];
		terrain_gpuvert_t *vtx = &out[k];
		const float		   sx = corner[q][0] ? size : -size, sy = corner[q][1] ? size : -size;
		for (int a = 0; a < 3; ++a)
			vtx->pos[a] = c[a] + rx[a] * sx + ry[a] * sy;
		vtx->light[0] = corner[q][0];
		vtx->light[1] = corner[q][1];
		vtx->light[2] = heat;
		vtx->light[3] = speed;
		vtx->params[0] = m.kind;
		vtx->params[1] = m.rnd;
		vtx->params[2] = m.flags;
		vtx->params[3] = m.amount;
	}
	return out + 6;
}

static float cost_cpu_ms, cost_gpu_ms;
static int	 last_solid, last_soft;

static void BindLava (cb_context_t *cbx, const vulkan_pipeline_t *pipe)
{
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipe);
	float constants[22];
	memcpy (constants, vulkan_globals.view_projection_matrix, 16 * sizeof (float));
	float fog_color[4];
	Fog_GetColor (fog_color);
	for (int c = 0; c < 3; ++c)
		constants[16 + c] = CLAMP (0.0f, fog_color[c], 1.0f);
	constants[19] = Fog_GetDensity () / 64.0f;
	constants[20] = (float)fmod (cl.time, 3600.0);
	constants[21] = (float)StyleCurrent ();
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof (constants), constants);
}

// With the terrain pipeline already bound (called after the props).
void R_TerrainLava_Draw (cb_context_t *cbx)
{
	const terrain_lava_view2_t *v = SV_Terrain_LavaView2 ();
	if (!v || (!v->count && !num_puffs))
		return;
	const double t_start = Sys_DoubleTime ();
	const int	 style = StyleCurrent ();
	const float	 glow = r_terrain_lava_glow.value;
	// size of the body quad per style, and which hot particles also get a glow quad
	static const float body_scale[STYLE_COUNT] = {1.2f, 1.95f, 1.35f, 1.3f};
	static const float glow_scale[STYLE_COUNT] = {2.8f, 3.3f, 0.0f, 2.6f};
	static const float glow_level[STYLE_COUNT] = {0.12f, 0.16f, 0.0f, 0.13f};
	const float		   body = v->radius * body_scale[style];
	const float		   far2 = 3000.0f * 3000.0f;
	const float		   cull = v->radius * 4.0f;
	const int		   want_glow = glow > 0.0f && glow_scale[style] > 0.0f;

	VkBuffer		   buffer;
	VkDeviceSize	   buffer_offset;
	terrain_gpuvert_t *solid = NULL, *soft = NULL, *solid_base = NULL, *soft_base = NULL;
	VkBuffer		   buffer2;
	VkDeviceSize	   buffer_offset2;
	if (v->count)
	{
		solid_base = solid = (terrain_gpuvert_t *)R_VertexAllocate (v->count * 6 * sizeof (terrain_gpuvert_t), &buffer, &buffer_offset);
	}
	soft_base = soft = (terrain_gpuvert_t *)R_VertexAllocate ((v->count / 2 + 1 + num_puffs) * 6 * sizeof (terrain_gpuvert_t), &buffer2, &buffer_offset2);

	for (int i = 0; i < v->count; ++i)
	{
		const float c[3] = {v->pos[0][i], v->pos[1][i], v->pos[2][i]};
		float		rel[3];
		VectorSubtract (c, r_refdef.vieworg, rel);
		const float depth = DotProduct (rel, vpn);
		if (DotProduct (rel, rel) > far2 || depth < -cull)
			continue;
		const float heat = CLAMP (0.0f, v->heat[i], 1.0f);
		const float vx = v->vel[0][i], vy = v->vel[1][i], vz = v->vel[2][i];
		const float speed = sqrtf (vx * vx + vy * vy + vz * vz);
		const byte	hb = (byte)(heat * 255.0f), sb = (byte)(CLAMP (0.0f, speed / 450.0f, 1.0f) * 255.0f);
		const byte	rb = (byte)((i * 2654435761u) >> 24);
		const float jit = 0.88f + 0.24f * ((float)rb / 255.0f);
		// the body is nudged toward the eye by part of its radius, so overlapping bodies stack like spheres
		const float front[3] = {c[0] - vpn[0] * body * 0.45f, c[1] - vpn[1] * body * 0.45f, c[2] - vpn[2] * body * 0.45f};
		quad_meta_t m = {0, rb, 0, 0};
		solid = PushQuad (solid, front, vright, vup, body * jit, hb, sb, m);
		if (want_glow && heat > 0.5f && (i & 1) == 0)
		{
			m.kind = 1;
			m.amount = (byte)(CLAMP (0.0f, glow_level[style] * glow * 2.0f * (heat - 0.3f) / 0.7f, 1.0f) * 255.0f);
			soft = PushQuad (soft, c, vright, vup, v->radius * glow_scale[style], hb, sb, m);
		}
	}
	for (int i = 0; i < num_puffs; ++i)
	{
		const puff_t *p = &puffs[i];
		float		  rel[3];
		VectorSubtract (p->pos, r_refdef.vieworg, rel);
		if (DotProduct (rel, rel) > far2 || DotProduct (rel, vpn) < -40.0f)
			continue;
		const float t = p->age / p->life;
		const float fade_in = CLAMP (0.0f, t / 0.12f, 1.0f), fade_out = (1.0f - t) * (1.0f - t);
		const float alpha = 0.5f * fade_in * fade_out;
		quad_meta_t m = {2, (byte)(p->rnd * 255.0f), 0, (byte)(CLAMP (0.0f, alpha, 1.0f) * 255.0f)};
		soft = PushQuad (soft, p->pos, vright, vup, p->size0 + p->grow * p->age, 0, 0, m);
	}
	last_solid = (int)(solid - solid_base) / 6;
	last_soft = (int)(soft - soft_base) / 6;
	if (solid_base && solid > solid_base)
	{
		BindLava (cbx, &vulkan_globals.lava_solid_pipeline[cbx->pipeline_variant]);
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
		vulkan_globals.vk_cmd_draw (cbx->cb, (uint32_t)(solid - solid_base), 1, 0, 0);
	}
	if (soft > soft_base)
	{
		BindLava (cbx, &vulkan_globals.lava_soft_pipeline[cbx->pipeline_variant]);
		vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer2, &buffer_offset2);
		vulkan_globals.vk_cmd_draw (cbx->cb, (uint32_t)(soft - soft_base), 1, 0, 0);
	}
	cost_cpu_ms += (float)((Sys_DoubleTime () - t_start) * 1000.0 - cost_cpu_ms) * 0.1f;
	cost_gpu_ms += ((float)rs_gputime_us / 1000.0f - cost_gpu_ms) * 0.1f;
}

void R_TerrainLava_PrintCost (void)
{
	const int cur = StyleCurrent ();
	Con_Printf ("terrain_lava_style: %s: %d body quads, %d soft quads (glow+steam), %d puffs; vertex fill %.3f ms cpu per frame; frame gpu %.2f ms (needs scr_speeds 1)\n", style_names[cur],
				last_solid, last_soft, num_puffs, cost_cpu_ms, cost_gpu_ms);
}
