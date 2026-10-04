#ifndef SURFACE_DITHER_INC
#define SURFACE_DITHER_INC

float SurfaceDitherHash (vec2 p)
{
	return fract (sin (dot (p, vec2 (127.1f, 311.7f))) * 43758.5453123f);
}

float SurfaceDitherFade (vec2 lightmap_uv)
{
	if (cluster_data.params.z <= 0.0f)
		return 0.0f;
	/* This derivative is intentionally evaluated before an alpha discard by the
	 * caller.  The seed remains anchored to quantized lightmap coordinates. */
	const vec2 texel_uv = lightmap_uv * vec2 (LMBLOCK_WIDTH * 16, LMBLOCK_HEIGHT * 16);
	return 1.0f / (1.0f + max (fwidth (texel_uv).x, fwidth (texel_uv).y));
}

vec3 ApplySurfaceDither (vec3 color, vec2 lightmap_uv, float fade)
{
	if (cluster_data.params.z <= 0.0f)
		return color;
	const vec2 seed = floor (lightmap_uv * vec2 (LMBLOCK_WIDTH * 16, LMBLOCK_HEIGHT * 16));
	const float triangular_noise = SurfaceDitherHash (seed) - SurfaceDitherHash (seed + vec2 (19.19f, 73.73f));
	const float amount = clamp (cluster_data.params.z, 0.0f, 2.0f) * fade * (1.0f / 255.0f);
	vec3 linear = sqrt (max (color, vec3 (0.0f)));
	linear = max (linear + triangular_noise * amount, vec3 (0.0f));
	return linear * linear;
}

#endif
