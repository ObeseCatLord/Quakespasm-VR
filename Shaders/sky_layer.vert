#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

#include "stereo.inc"

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	vec3  eye_pos;
}
push_constants;

layout (location = 0) in vec3 in_position;

layout (location = 0) out vec4 out_texcoord;

out gl_PerVertex
{
	vec4 gl_Position;
};

void main ()
{
	gl_Position = push_constants.mvp * vec4 (in_position, 1.0f);
	STEREO_APPLY_CLIP_CORRECTION ();
	out_texcoord = vec4 (in_position - (push_constants.eye_pos + STEREO_EYE_OFFSET ()), 0.0f);
	out_texcoord.z *= 3.0; // flatten the sphere
}
