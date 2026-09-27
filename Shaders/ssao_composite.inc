#ifdef STEREO
#extension GL_EXT_multiview : require
#endif
layout (push_constant) uniform constants
{
	vec4 viewport, projection, settings, composite_options;
}
pc;
#ifdef STEREO
#ifdef MULTISAMPLED
layout (set = 0, binding = 0) uniform usampler2DMSArray entity_mask;
layout (set = 1, binding = 0) uniform sampler2DMSArray scene_depth;
#define DEPTH(image, p, s) texelFetch (image, ivec3 (p, gl_ViewIndex), s).r
#else
layout (set = 0, binding = 0) uniform usampler2DArray entity_mask;
layout (set = 1, binding = 0) uniform sampler2DArray scene_depth;
#define DEPTH(image, p, s) texelFetch (image, ivec3 (p, gl_ViewIndex), 0).r
#endif
layout (set = 2, binding = 0) uniform sampler2DArray filtered_ao;
layout (set = 3, binding = 0) uniform sampler2DArray prepared_depth;
#define FETCH_PIXEL(image, p, s) texelFetch (image, ivec3 (p, gl_ViewIndex), s)
#else
#ifdef MULTISAMPLED
layout (set = 0, binding = 0) uniform usampler2DMS entity_mask;
layout (set = 1, binding = 0) uniform sampler2DMS scene_depth;
#define DEPTH(image, p, s) texelFetch (image, p, s).r
#else
layout (set = 0, binding = 0) uniform usampler2D entity_mask;
layout (set = 1, binding = 0) uniform sampler2D scene_depth;
#define DEPTH(image, p, s) texelFetch (image, p, 0).r
#endif
layout (set = 2, binding = 0) uniform sampler2D filtered_ao;
layout (set = 3, binding = 0) uniform sampler2D prepared_depth;
#define FETCH_PIXEL(image, p, s) texelFetch (image, p, s)
#endif
layout (location = 0) out vec4 out_color;

void main ()
{
#ifdef MULTISAMPLED
	gl_SampleMask[0] = -1;
#endif
	ivec2 p = ivec2 (gl_FragCoord.xy);
	int	  mask = 0;
#ifdef ENTITY_SSAO_DEBUG
	bool entity_pixel = false;
#endif
	float world = 0.0;
	// Only this inexpensive coverage check scales with MSAA, not GTAO or filtering.
#ifdef MULTISAMPLED
	for (int s = 0; s < int (pc.projection.w); ++s)
#else
	const int s = 0;
#endif
	{
		float scene = DEPTH (scene_depth, p, s);
		bool  entity = (DEPTH (entity_mask, p, s) & 2u) != 0u;
#ifdef ENTITY_SSAO_DEBUG
		entity_pixel = entity_pixel || entity;
#endif
		if (scene > 0.0 && !entity)
		{
			mask |= 1 << s;
			world = max (world, scene);
		}
	}
#ifdef ENTITY_SSAO_DEBUG
	if (pc.settings.w == 2.0)
	{
		out_color = vec4 (vec3 (entity_pixel ? 1.0 : 0.0), 1.0);
		return;
	}
	if (pc.settings.w == 3.0)
	{
		ivec2 depth_pixel = p;
#ifdef STEREO
		if (pc.composite_options.x > 0.0)
			depth_pixel = min (p / 2, textureSize (prepared_depth, 0).xy - 1);
#endif
		vec2 depths = FETCH_PIXEL (prepared_depth, depth_pixel, 0).xy;
		out_color = vec4 (vec3 (depths.y < depths.x ? 128.0 / (128.0 + depths.y) : 0.0), 1.0);
		return;
	}
	if (mask == 0 && pc.settings.w != 1.0)
#else
	if (mask == 0)
#endif
		discard;
#ifdef MULTISAMPLED
#ifdef ENTITY_SSAO_DEBUG
	gl_SampleMask[0] = pc.settings.w == 1.0 ? -1 : mask;
#else
	gl_SampleMask[0] = mask;
#endif
#endif
	float depth = world > 0.0 ? pc.projection.z / world : 1e9;
	float visibility;
#ifdef STEREO
	if (pc.composite_options.x > 0.0)
	{
		// The world-depth pyramid and AO share half-resolution texel centers.
		// Compare each candidate with this full-resolution receiver so a nearby
		// depth edge does not smear AO across thin geometry.
		vec2 low_position = (vec2 (p) + 0.5) * 0.5 - 0.5;
		ivec2 corner = ivec2 (floor (low_position));
		vec2 fraction = fract (low_position);
		ivec2 last = textureSize (filtered_ao, 0).xy - 1;
		float weighted = 0.0, total = 0.0;
		for (int y = 0; y < 2; ++y)
			for (int x = 0; x < 2; ++x)
			{
				ivec2 low_pixel = clamp (corner + ivec2 (x, y), ivec2 (0), last);
				float low_depth = FETCH_PIXEL (prepared_depth, low_pixel, 0).x;
				float low_visibility = FETCH_PIXEL (filtered_ao, low_pixel, 0).x;
				float delta = abs (low_depth - depth);
				float relative_delta = delta / max (1.0, depth * 0.01);
				float bilateral = 1.0 / (1.0 + relative_delta * relative_delta * relative_delta * relative_delta);
				float spatial = (x == 0 ? 1.0 - fraction.x : fraction.x) *
					(y == 0 ? 1.0 - fraction.y : fraction.y);
				float weight = spatial * bilateral;
				weighted += weight * low_visibility;
				total += weight;
			}
		// If all candidates lie on unrelated geometry, do not inherit their AO.
		// Fade back to unoccluded instead of making a dark silhouette halo.
		visibility = total > 0.0001 ? mix (1.0, weighted / total, min (1.0, 4.0 * total)) : 1.0;
	}
	else
#endif
		visibility = FETCH_PIXEL (filtered_ao, p, 0).x;
	float occlusion = 1.0 - visibility;
	// Match world_common.inc: fog uses view-space depth (clip W), not radial distance.
	float fog_distance = pc.settings.z * depth;
	float transmittance = exp (-fog_distance * fog_distance);
	float contribution = transmittance * pc.settings.y * occlusion;
	// Approximate linear-light attenuation when blending into Quake's gamma-encoded scene.
	contribution = 1.0 - pow (clamp (1.0 - contribution, 0.0, 1.0), 1.0 / 2.2);
#ifdef ENTITY_SSAO_DEBUG
	if (pc.settings.w == 1.0)
	{
		// Show the resolved multiplier, including samples excluded by the receiver mask.
#ifdef MULTISAMPLED
		contribution *= float (bitCount (uint (mask))) / pc.projection.w;
#else
		contribution *= mask != 0 ? 1.0 : 0.0;
#endif
		out_color = vec4 (vec3 (1.0 - contribution), 1.0);
		return;
	}
#endif
	out_color = vec4 (0.0, 0.0, 0.0, contribution);
}
