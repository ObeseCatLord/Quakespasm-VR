#ifndef CLUSTERED_LIGHTING_INC
#define CLUSTERED_LIGHTING_INC

#define CLUSTER_LIGHT_TILES_X 32
#define CLUSTER_LIGHT_TILES_Y 16
#define CLUSTER_LIGHT_TILES_Z 32
#define CLUSTER_LIGHT_COUNT (CLUSTER_LIGHT_TILES_X * CLUSTER_LIGHT_TILES_Y * CLUSTER_LIGHT_TILES_Z)
#define MAX_CLUSTER_LIGHTS 64

struct cluster_light_t
{
	vec3 origin;
	float radius;
	vec3 color;
	float minlight; // negative packs KEX intensity
	vec3 cone_dir;
	float cone_cos;
};

/* C ABI offsets: inverse VP 0, eye origin 128, eye forward 160, viewport
 * 192, params 208, counts 224, lights 240/stride48, masks 3312/stride8.
 * Each eye mask array spans 131072 bytes; frame slot spans 265456 bytes. */
layout (std430, set = 4, binding = 2) buffer clustered_lighting_buffer
{
	mat4 inverse_view_projection[2];
	vec4 eye_origin[2];
	vec4 eye_forward[2];
	vec4 viewport;
	vec4 params; // near, far, surface dither, clustered effective
	uvec4 counts; // fragment light count, eye count, conservative x/y padding
	cluster_light_t lights[MAX_CLUSTER_LIGHTS];
	uvec2 masks[2][CLUSTER_LIGHT_COUNT];
}
cluster_data;

#ifndef CLUSTER_LIGHTING_COMPUTE
struct cluster_surface_t
{
	vec3 world_pos;
	vec3 normal;
	uint eye;
};

bool ClusteredLightingActive ()
{
	return cluster_data.params.w != 0.0f && cluster_data.counts.x != 0u;
}

cluster_surface_t ClusterPrepareSurface (const uint instance_flags, const uint eye)
{
	cluster_surface_t surface;
	surface.eye = min (eye, cluster_data.counts.y - 1u);
	vec2 ndc_xy = ((gl_FragCoord.xy - cluster_data.viewport.xy) / cluster_data.viewport.zw) * 2.0f - 1.0f;
	float ndc_z = gl_FragCoord.z;
	/* The existing vertex z-fix offsets clip z before divide.  Restore the
	 * published NDC value with the matching 88-byte push-constant flag. */
	if ((instance_flags & 0x80000000u) != 0u)
		ndc_z += gl_FragCoord.w / 1024.0f;
	vec4 world = cluster_data.inverse_view_projection[surface.eye] * vec4 (ndc_xy, ndc_z, 1.0f);
	const float world_w = abs (world.w) > 1e-20f ? world.w : 1.0f;
	surface.world_pos = world.xyz / world_w;
	vec3 raw_normal = cross (dFdx (surface.world_pos), dFdy (surface.world_pos));
	const float len2 = dot (raw_normal, raw_normal);
	if (isnan (len2) || isinf (len2) || len2 < 1e-16f)
		surface.normal = vec3 (0.0f, 0.0f, 1.0f);
	else
		surface.normal = raw_normal * inversesqrt (len2);
	/* Native update_lightmap.inc uses BSP plane normal, negated for
	 * SURF_PLANEBACK, and lights only the side facing the eye.  The native
	 * Vulkan Y flip reverses derivative winding; orient in world space toward
	 * the actual eye rather than assuming gl_FrontFacing fixes that sign. */
	if (dot (surface.normal, cluster_data.eye_origin[surface.eye].xyz - surface.world_pos) < 0.0f)
		surface.normal = -surface.normal;
	return surface;
}

uint ClusterIndex (const cluster_surface_t surface)
{
	const float depth = max (dot (surface.world_pos - cluster_data.eye_origin[surface.eye].xyz, cluster_data.eye_forward[surface.eye].xyz), cluster_data.params.x);
	const float ratio = max (cluster_data.params.y / cluster_data.params.x, 1.0001f);
	uint z = uint (clamp (floor (log (depth / cluster_data.params.x) / log (ratio) * float (CLUSTER_LIGHT_TILES_Z)), 0.0f,
		float (CLUSTER_LIGHT_TILES_Z - 1)));
	vec2 tile = clamp (floor (((gl_FragCoord.xy - cluster_data.viewport.xy) / cluster_data.viewport.zw) *
		vec2 (CLUSTER_LIGHT_TILES_X, CLUSTER_LIGHT_TILES_Y)), vec2 (0.0f), vec2 (CLUSTER_LIGHT_TILES_X - 1, CLUSTER_LIGHT_TILES_Y - 1));
	return uint (tile.x) + uint (tile.y) * CLUSTER_LIGHT_TILES_X + z * CLUSTER_LIGHT_TILES_X * CLUSTER_LIGHT_TILES_Y;
}

vec3 ClusterDynamicLight (const cluster_surface_t surface)
{
	vec3 accumulated = vec3 (0.0f);
	const uvec2 mask_words = cluster_data.masks[surface.eye][ClusterIndex (surface)];
	for (uint word = 0u; word < 2u; ++word)
	{
		uint mask = mask_words[word];
		while (mask != 0u)
		{
			const uint bit = uint (findLSB (mask));
			mask &= ~(1u << bit);
			const uint index = word * 32u + bit;
			if (index >= cluster_data.counts.x)
				continue;
			const cluster_light_t light = cluster_data.lights[index];
			const float dist = length (surface.world_pos - light.origin);
			if (dist > light.radius - max (light.minlight, 0.0f))
				continue;
			const bool kex_light = light.minlight < 0.0f;
			float cone_scale = 1.0f;
			if (light.cone_cos > -1.0f && dist > 0.0f)
			{
				const float cone_dot = dot ((surface.world_pos - light.origin) / dist, light.cone_dir);
				if (kex_light)
				{
					if (cone_dot < light.cone_cos)
						continue;
					cone_scale = 1.0f - (1.0f - cone_dot) / max (1.0f - light.cone_cos, 1e-6f);
				}
				else
				{
					cone_scale = smoothstep (light.cone_cos, mix (light.cone_cos, 1.0f, 0.25f), cone_dot);
					if (cone_scale <= 0.0f)
						continue;
				}
			}
			float brightness;
			if (kex_light)
			{
				const float len = (light.radius - dist) / light.radius;
				const vec3 direction = (light.origin - surface.world_pos) / max (dist, 0.001f);
				brightness = -light.minlight * 0.5f * len * max (dot (surface.normal, direction), 0.0f);
			}
			else
				brightness = (light.radius - dist) / 256.0f;
			accumulated += max (brightness * cone_scale, 0.0f) * light.color;
		}
	}
	return accumulated;
}
#endif

#endif
