/*
 * Terrain look styles: the style table, the procedural texture atlas and the per-style fog.
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * The atlas is generated here at startup, nothing is loaded from disk: one 256x64 row per style,
 * four 64x64 tiles per row (wall ice, snow, old ice, emissive mask). Tiles are drawn at one texel
 * per Quake unit and fetched texel-exact in terrain.frag, so they read like the world's own textures.
 */
#include "quakedef.h"
#include "r_terrain_style.h"

#define TA_TILE 64
#define TA_W	(4 * TA_TILE)
#define TA_H	(4 * TA_TILE)

static const terrain_style_t terrain_styles[TERRAIN_STYLE_COUNT] = {
	{"ice (untextured)"},
	{
		"workshop",
		{1.25f, 1.40f, 1.75f},
		{1.00f, 0.93f, 0.80f},
		{0.06f, 0.10f, 0.20f},
		1.0f,
		0.35f,
		0.55f,
		0.55f,
		{0.0f},
		TERRAIN_PROPS_WORKSHOP,
	},
	{
		"glacier",
		{0.95f, 1.45f, 1.90f},
		{0.85f, 0.95f, 1.00f},
		{0.02f, 0.12f, 0.26f},
		0.85f,
		0.65f,
		0.62f,
		0.35f,
		{0.9f, 0.05f, 0.16f, 0.30f},
		TERRAIN_PROPS_GLACIER,
	},
	{
		"agartha",
		{0.80f, 0.70f, 1.35f},
		{1.00f, 0.72f, 0.42f},
		{0.05f, 0.03f, 0.10f},
		0.8f,
		0.25f,
		0.75f,
		0.0f,
		{0.6f, 0.04f, 0.02f, 0.08f},
		TERRAIN_PROPS_AGARTHA,
	},
};

static byte			terrain_atlas_data[TA_W * TA_H * 4];
static gltexture_t *terrain_atlas;
static int			terrain_style_applied = -1;
static qboolean		terrain_fog_owned;
static unsigned		terrain_props_epoch = ~0u;
static int			terrain_props_style = -1;

int R_TerrainStyle_Current (void)
{
	return CLAMP (0, (int)r_terrain_style.value, TERRAIN_STYLE_COUNT - 1);
}

const terrain_style_t *R_TerrainStyle_Get (void)
{
	return &terrain_styles[R_TerrainStyle_Current ()];
}

/* ---- procedural texture helpers: everything tiles with period 64 ---- */

static uint32_t TexHash (uint32_t seed, int x, int y)
{
	uint32_t h = seed * 0x9e3779b1u ^ (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	h *= 0x297a2d39u;
	h ^= h >> 15;
	return h;
}

static float TexRand (uint32_t seed, int x, int y)
{
	return (float)(TexHash (seed, x, y) & 0xffff) / 65535.0f;
}

// Tiled value noise with lattice spacing (cw, ch) texels, both dividing 64.
static float TexNoise (uint32_t seed, int x, int y, int cw, int ch)
{
	const int	nx = TA_TILE / cw, ny = TA_TILE / ch;
	const int	ix = x / cw, iy = y / ch;
	float		fx = (float)(x % cw) / (float)cw, fy = (float)(y % ch) / (float)ch;
	const float a = TexRand (seed, ix % nx, iy % ny), b = TexRand (seed, (ix + 1) % nx, iy % ny);
	const float c = TexRand (seed, ix % nx, (iy + 1) % ny), d = TexRand (seed, (ix + 1) % nx, (iy + 1) % ny);
	fx = fx * fx * (3.0f - 2.0f * fx);
	fy = fy * fy * (3.0f - 2.0f * fy);
	return (a + (b - a) * fx) * (1.0f - fy) + (c + (d - c) * fx) * fy;
}

static float TexFbm (uint32_t seed, int x, int y)
{
	return TexNoise (seed, x, y, 32, 32) * 0.45f + TexNoise (seed + 1, x, y, 16, 16) * 0.3f + TexNoise (seed + 2, x, y, 8, 8) * 0.17f +
		   TexNoise (seed + 3, x, y, 4, 4) * 0.08f;
}

// Tiled Voronoi on a grid of cell x cell texels: f1, f2 distances and the nearest site's id.
static void TexVoronoi (uint32_t seed, int x, int y, int cell, float *f1, float *f2, uint32_t *id)
{
	const int n = TA_TILE / cell;
	const int cx = x / cell, cy = y / cell;
	*f1 = *f2 = 1e9f;
	for (int oy = -1; oy <= 1; ++oy)
		for (int ox = -1; ox <= 1; ++ox)
		{
			const int	gx = cx + ox, gy = cy + oy;
			const int	wx = (gx + n) % n, wy = (gy + n) % n;
			const float sx = (float)(gx * cell) + TexRand (seed, wx, wy) * (float)cell;
			const float sy = (float)(gy * cell) + TexRand (seed ^ 0x55u, wx, wy) * (float)cell;
			const float dx = sx - ((float)x + 0.5f), dy = sy - ((float)y + 0.5f);
			const float d = sqrtf (dx * dx + dy * dy);
			if (d < *f1)
			{
				*f2 = *f1;
				*f1 = d;
				*id = TexHash (seed, wx, wy);
			}
			else if (d < *f2)
				*f2 = d;
		}
}

static void Mix3 (const float a[3], const float b[3], float t, float out[3])
{
	for (int c = 0; c < 3; ++c)
		out[c] = a[c] + (b[c] - a[c]) * t;
}

// Posterizes t to a few levels and maps it onto a dark..light ramp: the "paletted" look.
static void Ramp (const float dark[3], const float light[3], float t, int levels, float out[3])
{
	t = CLAMP (0.0f, t, 1.0f);
	t = floorf (t * (float)(levels - 1) + 0.5f) / (float)(levels - 1);
	Mix3 (dark, light, t, out);
}

// Nearest entry of the Quake palette, skipping the fullbright rows.
static void QuakePalette (float rgb[3])
{
	int best = 0, best_d = 1 << 30;
	const int r = (int)(rgb[0] * 255.0f), g = (int)(rgb[1] * 255.0f), b = (int)(rgb[2] * 255.0f);
	for (int i = 0; i < 224; ++i)
	{
		const byte *p = (const byte *)&d_8to24table[i];
		const int	dr = p[0] - r, dg = p[1] - g, db = p[2] - b;
		const int	d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
		if (d < best_d)
		{
			best_d = d;
			best = i;
		}
	}
	const byte *p = (const byte *)&d_8to24table[best];
	for (int c = 0; c < 3; ++c)
		rgb[c] = (float)p[c] / 255.0f;
}

static void Put (int tile, int row, int x, int y, const float rgb[3])
{
	byte *o = &terrain_atlas_data[((row * TA_TILE + y) * TA_W + tile * TA_TILE + x) * 4];
	for (int c = 0; c < 3; ++c)
		o[c] = (byte)(CLAMP (0.0f, rgb[c], 1.0f) * 255.0f + 0.5f);
	o[3] = 255;
}

/* ---- per-style tiles ---- */

static void GenWorkshop (int row)
{
	static const float ice_dark[3] = {0.28f, 0.42f, 0.62f}, ice_light[3] = {0.72f, 0.85f, 0.98f};
	static const float snow_dark[3] = {0.66f, 0.70f, 0.80f}, snow_light[3] = {0.95f, 0.96f, 1.0f};
	static const float old_dark[3] = {0.10f, 0.20f, 0.45f}, old_light[3] = {0.36f, 0.55f, 0.85f};
	for (int y = 0; y < TA_TILE; ++y)
		for (int x = 0; x < TA_TILE; ++x)
		{
			float rgb[3], f1, f2;
			uint32_t id = 0;
			// wall ice: vertical streaks, chunky facets and dark cracks along the facet borders
			TexVoronoi (11, x, y, 16, &f1, &f2, &id);
			float t = 0.35f + 0.35f * TexNoise (5, x, y, 4, 32) + 0.2f * TexFbm (7, x, y) + 0.12f * (float)(id & 255) / 255.0f;
			if (f2 - f1 < 1.1f)
				t *= 0.45f;
			else if (f2 - f1 < 2.2f)
				t += 0.12f;
			Mix3 (ice_dark, ice_light, CLAMP (0.0f, t, 1.0f), rgb);
			QuakePalette (rgb);
			Put (0, row, x, y, rgb);
			// snow: lumpy, with a few blue grains
			t = 0.55f + 0.45f * TexFbm (21, x, y);
			if (TexRand (23, x, y) < 0.03f)
				t -= 0.35f;
			Mix3 (snow_dark, snow_light, CLAMP (0.0f, t, 1.0f), rgb);
			QuakePalette (rgb);
			Put (1, row, x, y, rgb);
			// old blue ice: deep, with trapped bubbles
			t = 0.25f + 0.6f * TexFbm (31, x, y);
			if (TexRand (33, x, y) < 0.012f)
				t = 1.0f;
			Mix3 (old_dark, old_light, CLAMP (0.0f, t, 1.0f), rgb);
			QuakePalette (rgb);
			Put (2, row, x, y, rgb);
			static const float black[3] = {0, 0, 0};
			Put (3, row, x, y, black);
		}
}

static void GenGlacier (int row)
{
	static const float ice_dark[3] = {0.02f, 0.14f, 0.36f}, ice_light[3] = {0.36f, 0.70f, 0.98f};
	static const float snow_dark[3] = {0.46f, 0.66f, 0.86f}, snow_light[3] = {0.82f, 0.93f, 1.0f};
	static const float old_dark[3] = {0.0f, 0.05f, 0.20f}, old_light[3] = {0.10f, 0.36f, 0.72f};
	for (int y = 0; y < TA_TILE; ++y)
		for (int x = 0; x < TA_TILE; ++x)
		{
			float rgb[3], f1, f2;
			uint32_t id = 0;
			// wall ice: big crystal facets, each its own shade, lit along one edge
			TexVoronoi (41, x, y, 32, &f1, &f2, &id);
			float t = 0.2f + 0.5f * (float)(id & 255) / 255.0f + 0.15f * TexNoise (43, x, y, 8, 8) + 0.25f * CLAMP (0.0f, 1.0f - f1 / 22.0f, 1.0f);
			if (f2 - f1 < 1.0f)
				t += 0.3f;
			Ramp (ice_dark, ice_light, t, 9, rgb);
			Put (0, row, x, y, rgb);
			t = 0.45f + 0.55f * TexFbm (51, x, y);
			Ramp (snow_dark, snow_light, t, 6, rgb);
			Put (1, row, x, y, rgb);
			t = 0.15f + 0.7f * TexFbm (61, x, y);
			if (TexRand (63, x, y) < 0.015f)
				t = 1.0f;
			Ramp (old_dark, old_light, t, 7, rgb);
			Put (2, row, x, y, rgb);
			// emissive veins: a finer crack network, bright on its spine
			TexVoronoi (71, x, y, 16, &f1, &f2, &id);
			const float e = f2 - f1 < 0.9f ? 1.0f : (f2 - f1 < 1.9f ? 0.3f : 0.0f);
			const float vein[3] = {e, e, e};
			Put (3, row, x, y, (id & 3) ? vein : (const float[3]){0, 0, 0});
		}
}

static void GlyphStroke (float *mask, int x0, int y0, int x1, int y1)
{
	const int steps = q_max (abs (x1 - x0), abs (y1 - y0));
	for (int i = 0; i <= steps; ++i)
	{
		const int x = x0 + (steps ? (x1 - x0) * i / steps : 0), y = y0 + (steps ? (y1 - y0) * i / steps : 0);
		for (int oy = 0; oy < 2; ++oy)
			for (int ox = 0; ox < 2; ++ox)
				if (x + ox >= 0 && x + ox < 32 && y + oy >= 0 && y + oy < 32)
					mask[(y + oy) * 32 + x + ox] = 1.0f;
	}
}

// One 32x32 glyph from a seed: strokes between nodes of a 5x5 grid, sometimes inside a ring.
static void GenGlyph (uint32_t seed, float out[32 * 32])
{
	float core[32 * 32] = {0};
	uint32_t s = seed;
	int		 nx = 2, ny = (int)(TexHash (s, 0, 0) % 5);
	const int strokes = 4 + (int)(TexHash (s, 1, 0) % 4);
	for (int i = 0; i < strokes; ++i)
	{
		static const int dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, -1}, {1, -1}, {-1, 1}};
		const uint32_t	 h = TexHash (s, 2, i);
		const int		*d = dirs[h % 8];
		const int		 len = 1 + (int)((h >> 4) % 3);
		const int		 tx = CLAMP (0, nx + d[0] * len, 4), ty = CLAMP (0, ny + d[1] * len, 4);
		GlyphStroke (core, 4 + nx * 6, 4 + ny * 6, 4 + tx * 6, 4 + ty * 6);
		nx = tx;
		ny = ty;
		if ((h >> 9) % 4 == 0)
		{
			nx = (int)((h >> 12) % 5);
			ny = (int)((h >> 16) % 5);
		}
	}
	if (TexHash (s, 3, 0) % 2)
		for (int a = 0; a < 64; ++a)
		{
			const float ang = (float)a / 64.0f * 2.0f * (float)M_PI;
			const int	x = (int)(15.0f + 14.0f * cosf (ang)), y = (int)(15.0f + 14.0f * sinf (ang));
			GlyphStroke (core, x, y, x, y);
		}
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
		{
			float halo = 0.0f;
			for (int oy = -1; oy <= 1; ++oy)
				for (int ox = -1; ox <= 1; ++ox)
				{
					const int sx = x + ox, sy = y + oy;
					if (sx >= 0 && sx < 32 && sy >= 0 && sy < 32 && core[sy * 32 + sx] > 0.0f)
						halo = 0.3f;
				}
			out[y * 32 + x] = core[y * 32 + x] > 0.0f ? 1.0f : halo;
		}
}

static void GenAgartha (int row)
{
	static const float ice_dark[3] = {0.03f, 0.05f, 0.10f}, ice_light[3] = {0.26f, 0.34f, 0.48f};
	static const float snow_dark[3] = {0.28f, 0.28f, 0.38f}, snow_light[3] = {0.62f, 0.62f, 0.74f};
	static const float old_dark[3] = {0.04f, 0.03f, 0.12f}, old_light[3] = {0.22f, 0.18f, 0.44f};
	for (int y = 0; y < TA_TILE; ++y)
		for (int x = 0; x < TA_TILE; ++x)
		{
			float rgb[3];
			// wall: dark ice over cut stone courses, 32x16 blocks with offset joints
			const int bx = (x + ((y / 16) & 1) * 16) % 32, by = y % 16;
			const qboolean joint = bx == 0 || by == 0;
			float		   t = 0.3f + 0.4f * TexFbm (81, x, y) + 0.15f * TexRand (83, (x + ((y / 16) & 1) * 16) / 32, y / 16);
			if (joint)
				t *= 0.35f;
			else if (bx == 1 || by == 1)
				t += 0.12f;
			Ramp (ice_dark, ice_light, t, 8, rgb);
			Put (0, row, x, y, rgb);
			t = 0.4f + 0.6f * TexFbm (91, x, y);
			Ramp (snow_dark, snow_light, t, 5, rgb);
			Put (1, row, x, y, rgb);
			t = 0.2f + 0.7f * TexFbm (101, x, y);
			if (TexRand (103, x, y) < 0.01f)
				t = 1.0f;
			Ramp (old_dark, old_light, t, 7, rgb);
			Put (2, row, x, y, rgb);
		}
	// emissive tile: three glyphs in quadrants 0-2, a meander band in rows 12-19 of quadrant 3
	float glyph[32 * 32];
	for (int g = 0; g < 3; ++g)
	{
		GenGlyph (0x6a09e667u + (uint32_t)g * 977u, glyph);
		for (int y = 0; y < 32; ++y)
			for (int x = 0; x < 32; ++x)
			{
				const float e = glyph[y * 32 + x];
				Put (3, row, (g & 1) * 32 + x, (g >> 1) * 32 + y, (const float[3]){e, e, e});
			}
	}
	static const char *meander[8] = {"########", "......#.", ".####.#.", ".#..#.#.", ".#.##.#.", ".#....#.", ".######.", "........"};
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
		{
			float e = 0.0f;
			if (y >= 12 && y < 20)
				e = meander[y - 12][x % 8] == '#' ? 1.0f : 0.12f;
			Put (3, row, 32 + x, 32 + y, (const float[3]){e, e, e});
		}
}

static void R_TerrainStyle_BuildAtlas (void)
{
	memset (terrain_atlas_data, 0, sizeof (terrain_atlas_data));
	GenWorkshop (0);
	GenGlacier (1);
	GenAgartha (2);
	terrain_atlas = TexMgr_LoadImage (
		NULL, "terrain_atlas", TA_W, TA_H, SRC_RGBA, terrain_atlas_data, "", (src_offset_t)terrain_atlas_data,
		TEXPREF_MIPMAP | TEXPREF_PERSIST | TEXPREF_NOPICMIP);
}

/*
terrain_style [n | next]
Sets or cycles r_terrain_style and lists the styles.
*/
static void R_TerrainStyle_f (void)
{
	if (Cmd_Argc () > 1 && !strcmp (Cmd_Argv (1), "next"))
		Cvar_SetValue ("r_terrain_style", (float)((R_TerrainStyle_Current () + 1) % TERRAIN_STYLE_COUNT));
	else if (Cmd_Argc () > 1)
		Cvar_Set ("r_terrain_style", Cmd_Argv (1));
	for (int i = 0; i < TERRAIN_STYLE_COUNT; ++i)
		Con_Printf ("%s%d %s\n", i == R_TerrainStyle_Current () ? "* " : "  ", i, terrain_styles[i].name);
}

void R_TerrainStyle_Init (void)
{
	Cmd_AddCommand ("terrain_style", R_TerrainStyle_f);
}

/*
Main thread, once per frame: builds the atlas on first use, applies the style's fog when the style
changes, and rebuilds the props when the region or the style changes.
*/
void R_TerrainStyle_Update (const terrain_t *t, unsigned epoch)
{
	const int style = R_TerrainStyle_Current ();
	if (style > 0 && !terrain_atlas)
		R_TerrainStyle_BuildAtlas ();
	if (style != terrain_style_applied)
	{
		const terrain_style_t *st = &terrain_styles[style];
		if (st->fog[0] > 0.0f)
		{
			Cbuf_AddText (va ("fog %g %g %g %g\n", st->fog[0] * 0.1f, st->fog[1], st->fog[2], st->fog[3]));
			terrain_fog_owned = true;
		}
		else if (terrain_fog_owned)
		{
			Cbuf_AddText ("fog 0\n");
			terrain_fog_owned = false;
		}
		terrain_style_applied = style;
	}
	if (epoch != terrain_props_epoch || style != terrain_props_style)
	{
		terrain_props_epoch = epoch;
		terrain_props_style = style;
		R_TerrainProps_Rebuild (t);
	}
}

qboolean R_TerrainStyle_Bind (cb_context_t *cbx)
{
	if (!terrain_atlas)
		return false;
	R_BindPipeline (cbx, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.terrain_tex_pipeline[cbx->pipeline_variant]);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.basic_pipeline_layout.handle, 0, 1, &terrain_atlas->descriptor_set, 0, NULL);
	float constants[22];
	memcpy (constants, vulkan_globals.view_projection_matrix, 16 * sizeof (float));
	float fog_color[4]; // Fog_GetColor writes rgba
	Fog_GetColor (fog_color);
	for (int c = 0; c < 3; ++c)
		constants[16 + c] = CLAMP (0.0f, fog_color[c], 1.0f);
	constants[19] = Fog_GetDensity () / 64.0f;
	constants[20] = (float)fmod (cl.time, 3600.0);
	constants[21] = (float)R_TerrainStyle_Current ();
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof (constants), constants);
	return true;
}
