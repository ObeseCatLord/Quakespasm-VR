#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

// Textured terrain styles (r_terrain_style 1..3). The atlas holds one row of four 64x64 tiles per
// style: 0 wall ice, 1 snow, 2 old/pillar ice, 3 emissive masks. Every lookup is a texelFetch at an
// integer texel, so sampling is nearest-neighbour by construction, at one texel per Quake unit
// like the world's own textures; the mip level is picked per pixel and capped so it stays chunky.

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	float time;
	float style;
}
push_constants;

layout (set = 0, binding = 0) uniform sampler2D atlas;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec4 in_light;
layout (location = 2) flat in uvec4 in_params;
layout (location = 3) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

#define TILE 64
#define FLAG_FULLBRIGHT 1u // prop bulbs: the vertex colour is the final colour
#define FLAG_PLAIN		2u // prop cords and trunks: lit vertex colour, no texture
#define MAT_OLDICE		2u
#define MAT_GINGER		4u // platform gun disks

uint Hash (uvec3 v)
{
	uint h = v.x * 0x8da6b343u ^ v.y * 0xd8163841u ^ v.z * 0xcb1ab31fu;
	h ^= h >> 15;
	h *= 0x2c1b3c6du;
	h ^= h >> 12;
	h *= 0x297a2d39u;
	h ^= h >> 15;
	return h;
}

float ValueNoise (vec3 p)
{
	const vec3	i = floor (p);
	const vec3	f = smoothstep (0.0, 1.0, p - i);
	const uvec3 c = uvec3 (ivec3 (i) + 4096);
	float		n[8];
	for (int k = 0; k < 8; ++k)
		n[k] = float (Hash (c + uvec3 (k & 1, (k >> 1) & 1, k >> 2)) & 1023u) / 1023.0;
	return mix (mix (mix (n[0], n[1], f.x), mix (n[2], n[3], f.x), f.y), mix (mix (n[4], n[5], f.x), mix (n[6], n[7], f.x), f.y), f.z);
}

vec3 Fetch (int tile, int row, ivec2 texel, int lod)
{
	const ivec2 base = ivec2 (tile * TILE, row * TILE) >> lod;
	return texelFetch (atlas, base + ((texel & (TILE - 1)) >> lod), lod).rgb;
}

void main ()
{
	const uint axis = in_params.x;
	const uint material = in_params.y;
	const uint flags = in_params.z;
	const vec3 p = in_position;
	vec2	   uv = axis == 0u ? vec2 (p.y, -p.z) : (axis == 1u ? vec2 (p.x, -p.z) : vec2 (p.x, p.y));
	// derivatives first, while every lane of the quad is still running
	const vec2 du = dFdx (uv), dv = dFdy (uv);
	const int  lod = clamp (int (floor (log2 (max (max (length (du), length (dv)), 1e-4)) + 0.25)), 0, 3);
	const int  style = clamp (int (push_constants.style + 0.5), 1, 3);
	const int  row = style - 1;
	const ivec2 texel = ivec2 (floor (uv));

	vec3 color;
	if ((flags & FLAG_FULLBRIGHT) != 0u)
		color = in_light.rgb * 2.0;
	else
	{
		vec3 light = in_light.rgb * 2.0;
		vec3 albedo;
		if ((flags & FLAG_PLAIN) != 0u)
			albedo = vec3 (0.5);
		else if (material == MAT_GINGER)
		{
			// gingerbread: baked brown with darker crumb, the odd sugar sprinkle, in every style
			const uvec2 tb = uvec2 ((texel >> lod) + 65536);
			const uint	h = Hash (uvec3 (tb, 41u + axis));
			const float g = float (h & 1023u) / 1023.0;
			albedo = g > 0.72 ? vec3 (0.40, 0.20, 0.07) : vec3 (0.58, 0.31, 0.12);
			if (g > 0.997)
				albedo = vec3 (0.96, 0.94, 0.88); // icing
			else if (g > 0.994)
				albedo = ((h >> 12) & 1u) != 0u ? vec3 (0.80, 0.12, 0.10) : vec3 (0.18, 0.55, 0.22);
		}
		else
		{
			int tile = material == MAT_OLDICE ? 2 : 0;
			// snow settles in world-locked clumps of 4x4 texels with ragged single-texel edges,
			// so the boundary stays chunky instead of blurring or screen-dooring
			const uvec2 tb = uvec2 ((texel >> lod) + 65536);
			const float clump = float (Hash (uvec3 (tb >> 2u, 17u + axis)) & 255u) / 255.0;
			const float grain = float (Hash (uvec3 (tb, 29u + axis)) & 255u) / 255.0;
			if (in_light.a > 0.04 + 0.66 * clump + 0.3 * grain)
				tile = 1;
			albedo = Fetch (tile, row, texel, lod);
		}
		if (style == 1)
		{
			// software-Quake style light steps, on brightness only so the hue does not band
			const float level = max (max (light.r, light.g), max (light.b, 1e-4));
			light *= (floor (level * 12.0 + 0.5) / 12.0) / level;
		}
		color = albedo * light;

		if ((flags & FLAG_PLAIN) == 0u)
		{
			const ivec2 t0 = texel & (TILE - 1);
			if (style == 2)
			{
				// glacier: ice glows faintly from within, and patches of it carry pulsing veins
				color += albedo * vec3 (0.03, 0.08, 0.14);
				const float region = ValueNoise (p / 176.0);
				const float strength = smoothstep (0.6, 0.78, region);
				if (strength > 0.0)
				{
					const float vein = Fetch (3, row, texel, 0).r;
					const float pulse = 0.7 + 0.3 * sin (push_constants.time * 1.4 + region * 23.0);
					color += vein * strength * pulse * vec3 (0.25, 0.85, 1.0);
				}
			}
			else if (style == 3 && axis != 2u)
			{
				// Agartha: glyphs carved into some wall cells, and glowing bands at fixed depths
				const ivec2 gcell = ivec2 (floor (uv / 48.0));
				const uint	h = Hash (uvec3 (uvec2 (gcell + 8192), axis * 7u + 3u));
				const ivec2 local = texel - gcell * 48 - 8;
				float		glow = 0.0;
				vec3		tint = vec3 (1.0, 0.72, 0.25);
				if (h % 7u == 0u && all (greaterThanEqual (local, ivec2 (0))) && all (lessThan (local, ivec2 (32))))
				{
					const uint g = (h >> 8) % 3u;
					const ivec2 q = ivec2 (int (g & 1u) * 32, int (g >> 1) * 32);
					glow = texelFetch (atlas, ivec2 (3 * TILE, row * TILE) + q + local, 0).r;
					tint = ((h >> 12) & 1u) != 0u ? vec3 (0.35, 1.0, 0.8) : vec3 (1.0, 0.7, 0.25);
					glow *= 0.75 + 0.25 * sin (push_constants.time * 2.0 + float (h & 255u));
				}
				const float band_z = mod (p.z + 4096.0, 224.0);
				if (band_z < 8.0)
				{
					const ivec2 b = ivec2 ((texel.x & 31) + 32, 32 + 12 + int (band_z));
					glow = max (glow, texelFetch (atlas, ivec2 (3 * TILE, row * TILE) + b, 0).r * 0.8);
					tint = glow > 0.0 && band_z < 8.0 ? mix (tint, vec3 (0.55, 0.35, 1.0), 0.6) : tint;
				}
				color += glow * tint * 1.4;
			}
		}
	}

	const float fog = clamp (exp (-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord), 0.0, 1.0);
	out_frag_color = vec4 (mix (push_constants.fog_color, color, fog), 1.0);
}
