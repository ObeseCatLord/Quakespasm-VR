#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_GOOGLE_include_directive : enable

#include "stereo.inc"
#include "ui_panel_push_constants.inc"

layout (push_constant) uniform PushConsts
{
	mat4  mvp;
	vec3  fog_color;
	float fog_density;
	UI_PANEL_PUSH_CONSTANT_MEMBERS
}
push_constants;

layout (location = 0) in vec3 in_position;
layout (location = 1) in vec2 in_texcoord;
layout (location = 2) in vec4 in_color;

layout (location = 0) out vec4 out_texcoord;
layout (location = 1) out vec4 out_color;
layout (location = 2) out float out_fog_frag_coord;
#if defined(UI_PANEL)
layout (location = 4) out vec2 out_ui_position;
#endif

out gl_PerVertex
{
	vec4 gl_Position;
};

void main ()
{
	gl_Position = push_constants.mvp * vec4 (in_position, 1.0f);
#if defined(UI_PANEL)
	if (push_constants.ui_panel_enabled != 0.0f)
		STEREO_APPLY_CLIP_CORRECTION ();
#else
	STEREO_APPLY_CLIP_CORRECTION ();
#endif
	out_texcoord = vec4 (in_texcoord.xy, 0.0f, 0.0f);
	out_color = in_color;
	out_fog_frag_coord = gl_Position.w;
#if defined(UI_PANEL)
	out_ui_position = in_position.xy;
#endif
}
