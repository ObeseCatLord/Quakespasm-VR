#ifndef STEREO_INC
#define STEREO_INC

#if defined(STEREO)
#extension GL_EXT_multiview : require

layout (std140, set = 5, binding = 0) uniform StereoViews
{
	mat4 clip_from_center[2];
	vec4 eye_offset[2];
}
stereo_views;

#define STEREO_APPLY_CLIP_CORRECTION() gl_Position = stereo_views.clip_from_center[gl_ViewIndex] * gl_Position
#define STEREO_EYE_OFFSET() stereo_views.eye_offset[gl_ViewIndex].xyz
#else
#define STEREO_APPLY_CLIP_CORRECTION()
#define STEREO_EYE_OFFSET() vec3 (0.0f)
#endif

#endif
