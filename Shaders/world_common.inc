vec4 WorldFragmentColor ()
{
	vec4 diffuse = texture (diffuse_tex, in_texcoords.xy);
	/* Both derivative users must run before a cutout discard so neighboring
	 * lanes retain valid finite differences. */
	cluster_surface_t cluster_surface;
	const bool clustered = ClusteredLightingActive ();
	if (clustered)
		cluster_surface = ClusterPrepareSurface (in_instance_flags, in_eye_index);
	const float dither_fade = SurfaceDitherFade (in_texcoords.zw);
#if !HIDDEN_AREA_OPAQUE
	if (use_alpha_test && diffuse.a < 0.666f)
		discard;
#endif

	float lm_multiplier = scaled_lm ? 8.0f : 2.0f;
	vec3  light;

	if (quantize_lm)
	{
		ivec2 lm_size = ivec2 (LMBLOCK_WIDTH, LMBLOCK_HEIGHT);
		vec2  uv_exp = (floor ((lm_size * 16) * in_texcoords.zw) + 0.5) / (lm_size * 16);
		light = texture (lightmap_tex, uv_exp).rgb * lm_multiplier;
	}
	else
		light = texture (lightmap_tex, in_texcoords.zw).rgb * lm_multiplier;
	if (clustered)
	{
		/* Native update_lightmap.inc stores unscaled dynamic units for 8-bit,
		 * and multiplies by .25 for 10-bit. World then multiplies by 2/8:
		 * both paths yield 2x. Fragment contributions bypass atlas storage. */
		// Preserve native UNORM storage's combined-light range before diffuse.
		light = clamp (light + ClusterDynamicLight (cluster_surface) * 2.0f, vec3 (0.0f), vec3 (lm_multiplier));
	}

	vec4 frag_color;
	frag_color.rgb = diffuse.rgb * light.rgb;

	if (use_fullbright)
	{
		vec3 fullbright = texture (fullbright_tex, in_texcoords.xy).rgb;
		frag_color.rgb += fullbright;
	}

	float fog = exp (-push_constants.fog_density * push_constants.fog_density * in_fog_frag_coord * in_fog_frag_coord);
	fog = clamp (fog, 0.0, 1.0);
	frag_color.rgb = mix (push_constants.fog_color, frag_color.rgb, fog);
	frag_color.rgb = ApplySurfaceDither (frag_color.rgb, in_texcoords.zw, dither_fade);

	if (use_alpha_blend)
		frag_color.a = push_constants.alpha;

	return frag_color;
}
