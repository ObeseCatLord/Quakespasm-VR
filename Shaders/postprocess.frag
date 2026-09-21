#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (push_constant) uniform PushConsts
{
	float gamma;
	float contrast;
}
push_constants;

layout (input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput color_input;

layout (constant_id = 0) const bool xr_srgb_output = false;

layout (location = 0) out vec4 out_frag_color;

void main ()
{
	vec3 frag = subpassLoad (color_input).rgb;
	frag.rgb = frag.rgb * push_constants.contrast;
	frag = pow (frag, vec3 (push_constants.gamma));
	if (xr_srgb_output)
	{
		frag = clamp (frag, vec3 (0.0), vec3 (1.0));
		const vec3 linear = pow ((frag + vec3 (0.055)) / vec3 (1.055), vec3 (2.4));
		frag = mix (linear, frag / vec3 (12.92), lessThanEqual (frag, vec3 (0.04045)));
	}
	out_frag_color = vec4 (frag, 1.0);
}
