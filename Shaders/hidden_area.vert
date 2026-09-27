#version 460
#extension GL_EXT_multiview : require

layout (location = 0) in vec2 in_left;
layout (location = 1) in vec2 in_right;

void main ()
{
	gl_Position = vec4 (gl_ViewIndex == 0 ? in_left : in_right, 0.0, 1.0);
}
