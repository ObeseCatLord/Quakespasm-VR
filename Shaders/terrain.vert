#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

// Textured terrain (r_terrain.c). Positions are already in world space; the fragment shader
// derives nearest-texel box-projected texture coordinates from them.

#include "stereo.inc"

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
layout (location = 1) in vec4 in_light;	  // rgb: half the vertex light (so 1.0 is 2x overbright); a: snow cover
layout (location = 2) in uvec4 in_params; // x: projection axis, y: material, z: flags, w: ambient occlusion

layout (location = 0) out vec3 out_position;
layout (location = 1) out vec4 out_light;
layout (location = 2) flat out uvec4 out_params;
layout (location = 3) out float out_fog_frag_coord;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main ()
{
	gl_Position = push_constants.mvp * vec4 (in_position, 1.0f);
	STEREO_APPLY_CLIP_CORRECTION ();
	out_position = in_position;
	out_light = in_light;
	out_params = in_params;
	out_fog_frag_coord = gl_Position.w;
}
