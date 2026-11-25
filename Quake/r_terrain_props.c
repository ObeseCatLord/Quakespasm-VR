/*
 * Client-side clutter for the terrain styles: alias-model props (trees, presents, relics), strings
 * of lights drawn through the terrain pipeline, and the dynamic lights that go with them.
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * Everything is placed from the terrain seed when the region or the style changes, so the same
 * seed always dresses the same cave. Nothing here collides or is networked; props stay where they
 * were placed when the cave is dug away under them. Models are optional: a prop whose .mdl is not
 * in the game path is skipped (tests/link-tcj-props.sh links the Twisted Christmas ones into run/).
 */
#include "quakedef.h"
#include "r_terrain_style.h"

entity_t *CL_NewTempEntity (void); // cl_tent.c

#define MAX_TERRAIN_PROPS	 96
#define MAX_TERRAIN_STRINGS	 10
#define MAX_STRING_BULBS	 28
#define MAX_TERRAIN_GLOWS	 24
#define TERRAIN_LIGHT_KEY	 0x7e770000

typedef struct
{
	qmodel_t *model;
	vec3_t	  origin;
	float	  yaw;
	int		  skin;
	int		  frame;
} terrain_prop_t;

typedef struct
{
	vec3_t a, b; // anchors
	float  sag;
	int	   num_bulbs;
	vec3_t bulbs[MAX_STRING_BULBS];
	byte   colors[MAX_STRING_BULBS];
} terrain_string_t;

typedef struct
{
	vec3_t origin;
	float  radius;
	vec3_t color;
	float  flicker;
} terrain_glow_t;

static terrain_prop_t	terrain_props[MAX_TERRAIN_PROPS];
static int				terrain_num_props;
static terrain_string_t terrain_strings[MAX_TERRAIN_STRINGS];
static int				terrain_num_strings;
static terrain_glow_t	terrain_glows[MAX_TERRAIN_GLOWS];
static int				terrain_num_glows;
static const terrain_t *terrain_props_for;

static const float bulb_colors[5][3] = {{1.0f, 0.12f, 0.08f}, {0.15f, 1.0f, 0.2f}, {1.0f, 0.78f, 0.15f}, {0.2f, 0.45f, 1.0f}, {1.0f, 0.95f, 0.85f}};

static uint32_t PropRandom (uint32_t *s)
{
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return *s;
}

static float PropFrac (uint32_t *s)
{
	return (float)(PropRandom (s) & 0xffff) / 65535.0f;
}

// Sphere-traces the field along dir; true with the first surface point within max_dist.
static qboolean PropMarch (const terrain_t *t, const vec3_t start, const vec3_t dir, float max_dist, vec3_t hit)
{
	float travelled = 0.0f;
	while (travelled < max_dist)
	{
		VectorMA (start, travelled, dir, hit);
		const float d = Terrain_SampleWorld (t, hit);
		if (d <= 0.5f)
			return true;
		travelled += q_max (d * 0.8f, 1.0f);
	}
	return false;
}

static void PropNormal (const terrain_t *t, const vec3_t p, vec3_t n)
{
	for (int a = 0; a < 3; ++a)
	{
		vec3_t lo, hi;
		VectorCopy (p, lo);
		VectorCopy (p, hi);
		lo[a] -= 6.0f;
		hi[a] += 6.0f;
		n[a] = Terrain_SampleWorld (t, hi) - Terrain_SampleWorld (t, lo);
	}
	VectorNormalize (n);
}

// A random open point with this much clearance, at least min_dist from the region centre.
static qboolean PropOpenPoint (const terrain_t *t, uint32_t *rng, float clearance, vec3_t out)
{
	for (int attempt = 0; attempt < 400; ++attempt)
	{
		for (int a = 0; a < 3; ++a)
		{
			const float extent = (float)(t->size[a] * TERRAIN_CHUNK_UNITS);
			out[a] = (float)t->origin[a] + 96.0f + PropFrac (rng) * (extent - 192.0f);
		}
		if (Terrain_SampleWorld (t, out) > clearance)
			return true;
	}
	return false;
}

// A floor spot: an open point dropped onto a surface that faces up.
static qboolean PropFloor (const terrain_t *t, uint32_t *rng, float clearance, vec3_t out)
{
	static const vec3_t down = {0, 0, -1};
	for (int attempt = 0; attempt < 60; ++attempt)
	{
		vec3_t p, n;
		if (!PropOpenPoint (t, rng, clearance, p) || !PropMarch (t, p, down, 900.0f, out))
			continue;
		PropNormal (t, out, n);
		if (n[2] < 0.85f)
			continue;
		out[2] -= 2.0f; // sink the base a little so it does not float on bumps
		return true;
	}
	return false;
}

static qmodel_t *PropModel (const char *name)
{
	char path[MAX_QPATH];
	q_snprintf (path, sizeof (path), "%s", name);
	if (!COM_FileExists (path, NULL))
		return NULL;
	return Mod_ForName (path, false);
}

static void AddProp (qmodel_t *model, const vec3_t origin, float yaw, int skin)
{
	if (!model || terrain_num_props == MAX_TERRAIN_PROPS)
		return;
	terrain_prop_t *p = &terrain_props[terrain_num_props++];
	p->model = model;
	VectorCopy (origin, p->origin);
	p->yaw = yaw;
	p->skin = skin;
	p->frame = 0;
}

static void AddGlow (const vec3_t origin, float radius, float r, float g, float b, float flicker)
{
	if (terrain_num_glows == MAX_TERRAIN_GLOWS)
		return;
	terrain_glow_t *gl = &terrain_glows[terrain_num_glows++];
	VectorCopy (origin, gl->origin);
	gl->radius = radius;
	gl->color[0] = r;
	gl->color[1] = g;
	gl->color[2] = b;
	gl->flicker = flicker;
}

static void BuildStrings (const terrain_t *t, uint32_t *rng)
{
	static const vec3_t up = {0, 0, 1};
	for (int attempt = 0; attempt < 400 && terrain_num_strings < MAX_TERRAIN_STRINGS; ++attempt)
	{
		vec3_t floor_a, pa, ca, pb, cb;
		if (!PropFloor (t, rng, 40.0f, floor_a))
			continue;
		VectorCopy (floor_a, pa);
		pa[2] += 72.0f;
		if (Terrain_SampleWorld (t, pa) < 24.0f || !PropMarch (t, pa, up, 520.0f, ca) || ca[2] - floor_a[2] < 130.0f)
			continue;
		const float ang = PropFrac (rng) * 2.0f * (float)M_PI, span = 170.0f + PropFrac (rng) * 170.0f;
		VectorCopy (pa, pb);
		pb[0] += cosf (ang) * span;
		pb[1] += sinf (ang) * span;
		qboolean clear = true;
		for (int s = 1; s <= 16 && clear; ++s)
		{
			vec3_t q;
			for (int a = 0; a < 3; ++a)
				q[a] = pa[a] + (pb[a] - pa[a]) * (float)s / 16.0f;
			clear = Terrain_SampleWorld (t, q) > 12.0f;
		}
		if (!clear || !PropMarch (t, pb, up, 520.0f, cb) || fabsf (cb[2] - ca[2]) > 200.0f)
			continue;
		terrain_string_t *str = &terrain_strings[terrain_num_strings];
		VectorCopy (ca, str->a);
		VectorCopy (cb, str->b);
		str->a[2] -= 3.0f;
		str->b[2] -= 3.0f;
		const float len = sqrtf ((cb[0] - ca[0]) * (cb[0] - ca[0]) + (cb[1] - ca[1]) * (cb[1] - ca[1]) + (cb[2] - ca[2]) * (cb[2] - ca[2]));
		str->sag = 24.0f + len * 0.22f;
		str->num_bulbs = CLAMP (6, (int)(len / 14.0f), MAX_STRING_BULBS);
		int ok = 1;
		for (int i = 0; i < str->num_bulbs && ok; ++i)
		{
			const float f = ((float)i + 0.5f) / (float)str->num_bulbs;
			for (int a = 0; a < 3; ++a)
				str->bulbs[i][a] = str->a[a] + (str->b[a] - str->a[a]) * f;
			str->bulbs[i][2] -= str->sag * 4.0f * f * (1.0f - f) + 4.0f;
			str->colors[i] = (byte)((i + attempt) % 5);
			ok = Terrain_SampleWorld (t, str->bulbs[i]) > 3.0f;
		}
		if (!ok)
			continue;
		++terrain_num_strings;
		vec3_t mid;
		VectorCopy (str->bulbs[str->num_bulbs / 2], mid);
		const float *c = bulb_colors[terrain_num_strings % 4];
		AddGlow (mid, 240.0f, 0.55f + 0.45f * c[0], 0.55f + 0.45f * c[1], 0.55f + 0.45f * c[2], 0.15f);
	}
}

void R_TerrainProps_Rebuild (const terrain_t *t)
{
	terrain_num_props = terrain_num_strings = terrain_num_glows = 0;
	terrain_props_for = t;
	if (!t)
		return;
	const int set = R_TerrainStyle_Get ()->props;
	uint32_t  rng = t->seed * 0x2545f491u + 0x6d2b79f5u;
	if (!rng)
		rng = 1;
	vec3_t spot;
	if (set == TERRAIN_PROPS_WORKSHOP)
	{
		qmodel_t *xmas = PropModel ("progs/misc_xmastree.mdl");
		qmodel_t *tree = PropModel ("progs/kebby/balxj_tree_a.mdl"), *baubles = PropModel ("progs/kebby/balxj_tree_baubles.mdl");
		qmodel_t *gift = PropModel ("progs/kebby/kebby_xjgift.mdl");
		for (int i = 0; i < 7; ++i)
			if (PropFloor (t, &rng, 48.0f, spot))
			{
				const float yaw = PropFrac (&rng) * 360.0f;
				if ((i & 1) && tree)
				{
					AddProp (tree, spot, yaw, 0);
					AddProp (baubles, spot, yaw, 0);
				}
				else
					AddProp (xmas ? xmas : tree, spot, yaw, 0);
				// presents round the foot of each tree
				for (int g = 0; g < 3; ++g)
				{
					vec3_t gp, down = {0, 0, -1}, hit;
					const float ang = PropFrac (&rng) * 6.283f, r = 34.0f + PropFrac (&rng) * 20.0f;
					VectorCopy (spot, gp);
					gp[0] += cosf (ang) * r;
					gp[1] += sinf (ang) * r;
					gp[2] += 40.0f;
					if (Terrain_SampleWorld (t, gp) > 8.0f && PropMarch (t, gp, down, 120.0f, hit))
					{
						hit[2] -= 1.0f;
						AddProp (gift, hit, PropFrac (&rng) * 360.0f, (int)(PropRandom (&rng) % 4));
					}
				}
				vec3_t lamp;
				VectorCopy (spot, lamp);
				lamp[2] += 64.0f;
				AddGlow (lamp, 200.0f, 1.0f, 0.75f, 0.45f, 0.08f);
			}
		BuildStrings (t, &rng);
	}
	else if (set == TERRAIN_PROPS_GLACIER)
	{
		for (int i = 0; i < 5; ++i)
			if (PropOpenPoint (t, &rng, 48.0f, spot)) // the field saturates near 63 units
				AddGlow (spot, 380.0f, 0.15f, 0.55f, 0.85f, 0.0f);
	}
	else if (set == TERRAIN_PROPS_AGARTHA)
	{
		qmodel_t *plinth[2] = {PropModel ("progs/misc_plinth1.mdl"), PropModel ("progs/misc_plinth2.mdl")};
		qmodel_t *rune[4] = {PropModel ("progs/key_rune1.mdl"), PropModel ("progs/key_rune2.mdl"), PropModel ("progs/key_rune3.mdl"), PropModel ("progs/key_rune4.mdl")};
		qmodel_t *pots[3] = {PropModel ("progs/brk_pot1a.mdl"), PropModel ("progs/brk_pot2a.mdl"), PropModel ("progs/brk_vase1.mdl")};
		qmodel_t *statue[3] = {PropModel ("progs/misc_jstatue_02.mdl"), PropModel ("progs/misc_jstatue_03.mdl"), PropModel ("progs/misc_tutstatue.mdl")};
		for (int i = 0; i < 6; ++i)
			if (PropFloor (t, &rng, 48.0f, spot))
			{
				const float yaw = (float)(PropRandom (&rng) % 4) * 90.0f;
				AddProp (plinth[i & 1], spot, yaw, 0);
				vec3_t top;
				VectorCopy (spot, top);
				top[2] += 30.0f;
				AddProp (rune[i & 3], top, yaw, 0);
				AddGlow (top, 220.0f, (i & 1) ? 0.35f : 0.9f, (i & 1) ? 1.0f : 0.65f, (i & 1) ? 0.8f : 0.3f, 0.05f);
			}
		for (int i = 0; i < 5; ++i)
			if (PropFloor (t, &rng, 48.0f, spot))
				AddProp (statue[i % 3], spot, PropFrac (&rng) * 360.0f, 0);
		for (int i = 0; i < 10; ++i)
			if (PropFloor (t, &rng, 40.0f, spot))
			{
				AddProp (pots[PropRandom (&rng) % 3], spot, PropFrac (&rng) * 360.0f, 0);
				if ((i % 3) == 0) // an unseen fire nearby: warm light on the pots
				{
					spot[2] += 24.0f;
					AddGlow (spot, 170.0f, 1.0f, 0.62f, 0.3f, 0.35f);
				}
			}
	}
	Con_DPrintf ("terrain: style %d props %d strings %d lights %d\n", R_TerrainStyle_Current (), terrain_num_props, terrain_num_strings, terrain_num_glows);
}

/*
Client frame, from CL_UpdateTEnts: the props go into the visible entity list as temporary
entities and their lights into cl_dlights, so monsters and the player's weapon see them too.
*/
void R_Terrain_AddEntities (void)
{
	if (!terrain_props_for || terrain_props_for != terrain_active || !r_terrain_props.value || R_TerrainStyle_Current () == 0)
		return;
	for (int i = 0; i < terrain_num_props; ++i)
	{
		const terrain_prop_t *p = &terrain_props[i];
		entity_t			 *ent = CL_NewTempEntity ();
		if (!ent)
			break;
		ent->model = p->model;
		VectorCopy (p->origin, ent->origin);
		ent->angles[1] = p->yaw;
		ent->skinnum = p->skin;
		ent->frame = p->frame;
	}
	for (int i = 0; i < terrain_num_glows; ++i)
	{
		const terrain_glow_t *g = &terrain_glows[i];
		dlight_t			 *dl = CL_AllocDlight (TERRAIN_LIGHT_KEY + i);
		VectorCopy (g->origin, dl->origin);
		const float wobble = 1.0f - g->flicker * (0.5f + 0.5f * sinf ((float)cl.time * 9.0f + (float)i * 2.3f) * sinf ((float)cl.time * 3.7f + (float)i));
		dl->radius = g->radius * wobble;
		dl->die = cl.time + 0.1;
		VectorCopy (g->color, dl->color);
	}
}

static int PushBulb (terrain_gpuvert_t *out, const vec3_t c, float r, const byte rgb[3])
{
	static const float dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	static const int   faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
	int				   n = 0;
	for (int f = 0; f < 8; ++f)
		for (int k = 0; k < 3; ++k)
		{
			terrain_gpuvert_t *v = &out[n++];
			const float		  *d = dirs[faces[f][k]];
			const float		   stretch = d[2] < 0.0f ? 1.6f : 1.0f; // bulbs hang to a point
			for (int a = 0; a < 3; ++a)
				v->pos[a] = c[a] + d[a] * r * stretch;
			v->light[0] = rgb[0];
			v->light[1] = rgb[1];
			v->light[2] = rgb[2];
			v->light[3] = 0;
			v->params[0] = 2;
			v->params[1] = 0;
			v->params[2] = TERRAIN_GPU_FULLBRIGHT;
			v->params[3] = 255;
		}
	return n;
}

static int PushCord (terrain_gpuvert_t *out, const vec3_t a, const vec3_t b)
{
	static const byte cord[3] = {14, 30, 16};
	const vec3_t	  offs[2] = {{0.8f, 0.0f, 0.0f}, {0.0f, 0.8f, 0.0f}};
	int				  n = 0;
	for (int s = 0; s < 2; ++s)
	{
		vec3_t q[4];
		VectorAdd (a, offs[s], q[0]);
		VectorSubtract (a, offs[s], q[1]);
		VectorSubtract (b, offs[s], q[2]);
		VectorAdd (b, offs[s], q[3]);
		static const int order[6] = {0, 1, 2, 0, 2, 3};
		for (int k = 0; k < 6; ++k)
		{
			terrain_gpuvert_t *v = &out[n++];
			VectorCopy (q[order[k]], v->pos);
			v->light[0] = cord[0];
			v->light[1] = cord[1];
			v->light[2] = cord[2];
			v->light[3] = 0;
			v->params[0] = 2;
			v->params[1] = 0;
			v->params[2] = TERRAIN_GPU_FULLBRIGHT;
			v->params[3] = 255;
		}
	}
	return n;
}

// Strings of lights, drawn with the terrain pipeline already bound.
void R_TerrainProps_Draw (cb_context_t *cbx)
{
	if (!terrain_num_strings || terrain_props_for != terrain_active || !r_terrain_props.value)
		return;
	int total = 0;
	for (int s = 0; s < terrain_num_strings; ++s)
		total += terrain_strings[s].num_bulbs * 24 + (terrain_strings[s].num_bulbs + 1) * 12;
	VkBuffer		   buffer;
	VkDeviceSize	   buffer_offset;
	terrain_gpuvert_t *out = (terrain_gpuvert_t *)R_VertexAllocate (total * sizeof (terrain_gpuvert_t), &buffer, &buffer_offset);
	int				   n = 0;
	const float		   time = (float)cl.time;
	for (int s = 0; s < terrain_num_strings; ++s)
	{
		const terrain_string_t *str = &terrain_strings[s];
		vec3_t					prev;
		VectorCopy (str->a, prev);
		for (int i = 0; i < str->num_bulbs; ++i)
		{
			vec3_t knot;
			VectorCopy (str->bulbs[i], knot);
			knot[2] += 3.0f;
			n += PushCord (out + n, prev, knot);
			// three chases running along the string, in steps like old fairy lights
			const int	 on = ((int)(time * 3.0f) + i) % 3 != 0;
			const float *c = bulb_colors[str->colors[i]];
			const float	 level = on ? 0.5f : 0.16f; // light is halved: 0.5 is full brightness
			const byte	 rgb[3] = {(byte)(c[0] * level * 255.0f), (byte)(c[1] * level * 255.0f), (byte)(c[2] * level * 255.0f)};
			n += PushBulb (out + n, str->bulbs[i], 3.5f, rgb);
			VectorCopy (knot, prev);
		}
		n += PushCord (out + n, prev, str->b);
	}
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	vulkan_globals.vk_cmd_draw (cbx->cb, n, 1, 0, 0);
}
