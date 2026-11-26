#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

// Particle lava (r_terrain_lava.c), drawn with the terrain vertex shader. One camera-facing quad per
// particle or puff. Vertex data: in_light.rg corner (0..1), .b heat, .a speed; params: x kind
// (0 body, 1 glow, 2 steam), y per-particle random, z flags, w glow or steam strength.
// push_constants.style picks the look of the bodies: 0 blobs, 1 goo, 2 chunky, 3 crust.
// Solid pipeline: opaque, writes depth. Soft pipeline: premultiplied alpha, alpha 0 adds.

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	float time;
	float style;
}
push_constants;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec4 in_light;
layout (location = 2) flat in uvec4 in_params;
layout (location = 3) in float in_fog_frag_coord;

layout (location = 0) out vec4 out_frag_color;

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

float Bayer4 (ivec2 p)
{
	const int m[16] = int[16] (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
	return (float (m[(p.y & 3) * 4 + (p.x & 3)]) + 0.5) / 16.0;
}

// heat 0..1: black rock, dull red, red, orange, yellow, hot yellow-white
vec3 Ramp (float t)
{
	t = clamp (t, 0.0, 1.0) * 0.78;
	const vec3 c0 = vec3 (0.03, 0.004, 0.0), c1 = vec3 (0.20, 0.02, 0.0), c2 = vec3 (0.55, 0.08, 0.01), c3 = vec3 (0.95, 0.26, 0.03), c4 = vec3 (1.0, 0.46, 0.06),
			   c5 = vec3 (1.0, 0.70, 0.22);
	if (t < 0.15)
		return mix (c0, c1, t / 0.15);
	if (t < 0.35)
		return mix (c1, c2, (t - 0.15) / 0.20);
	if (t < 0.60)
		return mix (c2, c3, (t - 0.35) / 0.25);
	if (t < 0.82)
		return mix (c3, c4, (t - 0.60) / 0.22);
	return mix (c4, c5, (t - 0.82) / 0.18);
}

vec3 Palette6 (int k)
{
	// a Quake-ish lava palette: nearly black red up to bright yellow
	const vec3 pal[6] = vec3[6] (vec3 (0.10, 0.0, 0.0), vec3 (0.30, 0.02, 0.0), vec3 (0.60, 0.07, 0.0), vec3 (0.88, 0.24, 0.02), vec3 (1.0, 0.50, 0.08), vec3 (1.0, 0.80, 0.25));
	return pal[clamp (k, 0, 5)];
}

void main ()
{
	const uint	kind = in_params.x;
	const float rnd = float (in_params.y) / 255.0;
	const float amount = float (in_params.w) / 255.0;
	const vec2	uv = in_light.rg * 2.0 - 1.0;
	const float r2 = dot (uv, uv);
	const float heat = in_light.b;
	const float spd = in_light.a;
	const int	style = int (push_constants.style + 0.5);
	const float fog = clamp (exp (-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord), 0.0, 1.0);
	const ivec2 frag = ivec2 (gl_FragCoord.xy);

	if (kind == 2u)
	{
		// steam: a soft, slowly churning puff
		if (r2 >= 1.0)
			discard;
		const float soft = (1.0 - r2) * (1.0 - r2);
		const float n = ValueNoise (vec3 (uv * 1.6 + rnd * 31.0, push_constants.time * 0.5 + rnd * 11.0));
		const float a = amount * soft * (0.45 + 0.8 * n);
		const vec3	col = mix (push_constants.fog_color, vec3 (0.80, 0.82, 0.86), fog);
		out_frag_color = vec4 (col * a, a);
		return;
	}
	if (kind == 1u)
	{
		// glow: adds a little light around hot particles
		if (r2 >= 1.0)
			discard;
		const float k = (1.0 - r2) * (1.0 - r2) * amount;
		out_frag_color = vec4 (Ramp (0.50 + 0.4 * heat) * k * fog, 0.0);
		return;
	}

	if (r2 >= 1.0)
		discard;
	const float rr = sqrt (r2);
	const float nz = sqrt (1.0 - r2);
	vec3		color;
	if (style == 0)
	{
		// blobs: a sphere with a hot core and a darker rim
		const float core = pow (nz, 1.6);
		color = Ramp (heat * (0.84 + 0.10 * rnd) + 0.20 * core * heat) * (0.50 + 0.80 * core);
		color *= mix (0.45, 1.0, smoothstep (1.0, 0.60, rr));
		color *= 0.92 + 0.08 * uv.y;
	}
	else if (style == 1)
	{
		// goo: big overlapping bodies with no dark outline, so they read as one mass; the silhouette is dithered
		if (rr > 0.76 && Bayer4 (frag) < (rr - 0.76) / 0.24)
			discard;
		const float thick = nz;
		color = Ramp (heat * (0.80 + 0.20 * thick)) * (0.72 + 0.46 * thick);
		const vec3	nrm = normalize (vec3 (uv, nz));
		const float spec = pow (max (dot (nrm, normalize (vec3 (-0.35, 0.55, 0.75))), 0.0), 22.0);
		color += vec3 (1.0, 0.78, 0.45) * spec * 0.30 * heat;
	}
	else if (style == 2)
	{
		// chunky: a pixel-art disc, a lava texture warping over world-locked 2 unit texels, six palette steps, ordered dither
		const float cells = 7.0;
		const vec2	uvq = (floor ((uv * 0.5 + 0.5) * cells) + 0.5) / cells * 2.0 - 1.0;
		if (dot (uvq, uvq) > 1.0)
			discard;
		const float t = push_constants.time;
		const vec3	cell = floor (in_position * 0.5);
		const vec3	warp = vec3 (sin (cell.y * 0.37 + t * 1.3), cos (cell.x * 0.31 + t * 1.1), sin (cell.z * 0.29 + t * 0.9)) * 2.2;
		const float pat = ValueNoise ((cell + warp) * 0.16 + vec3 (0.0, 0.0, t * 0.25)) * 0.65 + ValueNoise ((cell - warp) * 0.37 + 11.0) * 0.35;
		float		level = heat * 0.64 + (pat - 0.5) * 0.62 + 0.10 * nz;
		level += (Bayer4 (frag >> 1) - 0.5) * 0.14;
		const int k = int (floor (level * 6.0));
		color = Palette6 (heat < 0.1 ? 0 : k);
	}
	else
	{
		// crust: hot and moving is yellow-white; as heat drops black plates grow, held apart by glowing cracks
		if (rr > 0.84 && Bayer4 (frag) < (rr - 0.84) / 0.16)
			discard;
		const float eff = clamp (heat + spd * 0.45, 0.0, 1.0);
		const vec3	p = in_position;
		const float n1 = ValueNoise (p * 0.065 + vec3 (0.0, 0.0, push_constants.time * 0.03));
		const float n2 = ValueNoise (p * 0.17 + 13.0);
		const float ridge = abs (n1 - 0.5) * 2.0; // 0 along crack lines
		const float crack = 1.0 - smoothstep (0.0, 0.14 + 0.30 * eff, ridge);
		const float cover = smoothstep (0.60, 0.18, eff + (n2 - 0.5) * 0.35);
		const vec3	plate = vec3 (0.045, 0.028, 0.024) * (0.7 + 0.7 * n2) + Ramp (eff * 0.6) * 0.30 * (1.0 - cover);
		const vec3	glow = Ramp (0.30 + 0.70 * eff + 0.10 * n2) * (0.78 + 0.45 * nz);
		color = mix (glow, plate, cover * (1.0 - crack));
		color *= mix (0.55, 1.0, smoothstep (1.0, 0.65, rr));
	}
	out_frag_color = vec4 (mix (push_constants.fog_color, color, fog), 1.0);
}
