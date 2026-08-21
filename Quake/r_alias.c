/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

//r_alias.c -- alias model rendering

#include "quakedef.h"

extern cvar_t r_drawflat, gl_overbright_models, gl_fullbrights, r_lerpmodels, r_lerpmove; //johnfitz
extern cvar_t scr_fov, cl_gun_fovscale, vr_enabled;
extern vec3_t vr_weaponcolor;

cvar_t r_alias_batching = {"r_alias_batching", "1", CVAR_ARCHIVE};

//up to 16 color translated skins
gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz -- changed to an array of pointers

const float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

extern vec3_t	lightcolor; //johnfitz -- replaces "float shadelight" for lit support

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
static const float	r_avertexnormal_dots[SHADEDOT_QUANT][256] = {
#include "anorm_dots.h"
};

extern	vec3_t	lightspot;

static const float	*shadedots = r_avertexnormal_dots[0];
static vec3_t	shadevector;

static float	entalpha; //johnfitz

static qboolean overbright; //johnfitz

static qboolean shading = true; //johnfitz -- if false, disable vertex shading for various reasons (fullbright, r_lightmap, showtris, etc)

static qboolean r_alias_batch_scope;
static qboolean r_alias_glsl_batch_active;
static qmodel_t *r_alias_glsl_batch_model;

//johnfitz -- struct for passing lerp information to drawing functions
typedef struct {
	short pose1;
	short pose2;
	float blend;
	vec3_t origin;
	vec3_t angles;
} lerpdata_t;
//johnfitz

/*
 * This deliberately only batches equal poses.  The compatibility alias VBO
 * stores every pose as a separate vertex stream, so selecting different pose
 * streams per instance needs buffer textures or shader storage that the
 * baseline #version 110 renderer cannot rely on.  Transform and lighting do
 * remain genuinely per-instance.
 */
#define MAX_ALIAS_INSTANCES 128
typedef struct {
	float matrix[16];
	float shadeblend[4];
	float lightalpha[4];
} alias_instance_t;

static alias_instance_t r_alias_instances[MAX_ALIAS_INSTANCES];
static int r_alias_instance_count;
static GLuint r_alias_instance_vbo;
static qmodel_t *r_alias_instance_model;
static aliashdr_t *r_alias_instance_hdr;
static gltexture_t *r_alias_instance_texture;
static short r_alias_instance_pose1;
static short r_alias_instance_pose2;
static qboolean r_alias_instance_overbright;

static GLuint r_alias_program;
static GLuint r_alias_instanced_program;
static GLuint r_md3_program;
static GLuint r_md5_program;
static qboolean r_md3_glsl_active;
static qboolean r_md3_glsl_alphatest;
static qboolean r_md5_glsl_active;
static qboolean r_md5_glsl_alphatest;

static qboolean r_vrik_pose_pending;
static qboolean r_vrik_skin_active;
static vrik_pose_t r_vrik_pending_pose;
static const aliashdr_t *r_vrik_skin_surface;
static md5vertex_t *r_vrik_skin_vertices;
static int r_vrik_skin_capacity;
static float r_vrik_palette[MAX_MD5_JOINTS * 12];

// uniforms used in vert shader
static GLint  blendLoc;
static GLint  shadevectorLoc;
static GLint  lightColorLoc;

// uniforms used in frag shader
static GLint  texLoc;
static GLint  fullbrightTexLoc;
static GLint  useFullbrightTexLoc;
static GLint  useOverbrightLoc;
static GLint  useAlphaTestLoc;
static GLint  instancedTexLoc;
static GLint  instancedUseOverbrightLoc;

static GLint  md3BlendLoc;
static GLint  md3ShadevectorLoc;
static GLint  md3LightColorLoc;
static GLint  md3TexLoc;
static GLint  md3UseOverbrightLoc;
static GLint  md3UseAlphaTestLoc;
static GLint  md3UseShadingLoc;
static GLint  md3UseTextureLoc;
static GLint  md3UseFogLoc;

static GLint  md5BlendLoc;
static GLint  md5ShadevectorLoc;
static GLint  md5LightColorLoc;
static GLint  md5TexLoc;
static GLint  md5TexSScaleLoc;
static GLint  md5TexTScaleLoc;
static GLint  md5UseAlphaTestLoc;
static GLint  md5UseShadingLoc;
static GLint  md5UseTextureLoc;
static GLint  md5UseFogLoc;

#define pose1VertexAttrIndex 0
#define pose1NormalAttrIndex 1
#define pose2VertexAttrIndex 2
#define pose2NormalAttrIndex 3
#define texCoordsAttrIndex 4
#define instanceMatrix0AttrIndex 5
#define instanceMatrix1AttrIndex 6
#define instanceMatrix2AttrIndex 7
#define instanceMatrix3AttrIndex 8
#define instanceShadeBlendAttrIndex 9
#define instanceLightAlphaAttrIndex 10

/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static void *GLARB_GetXYZOffsetForModel (qmodel_t *model, aliashdr_t *hdr, int pose)
{
	const int xyzoffs = offsetof (meshxyz_t, xyz);
	return (void *) (model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + xyzoffs);
}

static void *GLARB_GetXYZOffset (aliashdr_t *hdr, int pose)
{
	return GLARB_GetXYZOffsetForModel (currententity->model, hdr, pose);
}

/*
=============
GLARB_GetNormalOffset

Returns the offset of the first vertex's meshxyz_t.normal in the vbo for the
given model and pose.
=============
*/
static void *GLARB_GetNormalOffsetForModel (qmodel_t *model, aliashdr_t *hdr, int pose)
{
	const int normaloffs = offsetof (meshxyz_t, normal);
	return (void *)(model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + normaloffs);
}

static void *GLARB_GetNormalOffset (aliashdr_t *hdr, int pose)
{
	return GLARB_GetNormalOffsetForModel (currententity->model, hdr, pose);
}

static void GL_AliasInstanced_Flush (void);

static void GL_AliasBatch_End (void)
{
	GL_AliasInstanced_Flush ();

	if (!r_alias_glsl_batch_active)
		return;

	if (r_perfdebug.value)
		r_perf_alias_batch_flushes++;

	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2NormalAttrIndex);

	GL_UseProgram (0);
	GL_SelectTexture (GL_TEXTURE0);

	r_alias_glsl_batch_active = false;
	r_alias_glsl_batch_model = NULL;
}

static void AliasMatrixIdentity (float *m)
{
	memset (m, 0, 16 * sizeof (*m));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void AliasMatrixPostMultiply (float *m, const float *rhs)
{
	float result[16];
	int column, row, k;

	for (column = 0; column < 4; column++)
		for (row = 0; row < 4; row++)
		{
			result[column * 4 + row] = 0.0f;
			for (k = 0; k < 4; k++)
				result[column * 4 + row] += m[k * 4 + row] * rhs[column * 4 + k];
		}
	memcpy (m, result, sizeof (result));
}

static void AliasMatrixPostTranslate (float *m, float x, float y, float z)
{
	float translate[16];
	AliasMatrixIdentity (translate);
	translate[12] = x;
	translate[13] = y;
	translate[14] = z;
	AliasMatrixPostMultiply (m, translate);
}

static void AliasMatrixPostScale (float *m, float x, float y, float z)
{
	float scale[16];
	AliasMatrixIdentity (scale);
	scale[0] = x;
	scale[5] = y;
	scale[10] = z;
	AliasMatrixPostMultiply (m, scale);
}

static void AliasMatrixPostRotate (float *m, float degrees, float x, float y, float z)
{
	float rotate[16];
	float radians = degrees * (float)(M_PI / 180.0);
	float c = cosf (radians);
	float s = sinf (radians);

	AliasMatrixIdentity (rotate);
	if (z != 0.0f)
	{
		rotate[0] = c;
		rotate[1] = s;
		rotate[4] = -s;
		rotate[5] = c;
	}
	else if (y != 0.0f)
	{
		rotate[0] = c;
		rotate[2] = -s;
		rotate[8] = s;
		rotate[10] = c;
	}
	else
	{
		rotate[5] = c;
		rotate[6] = s;
		rotate[9] = -s;
		rotate[10] = c;
	}
	AliasMatrixPostMultiply (m, rotate);
}

static void AliasInstanceBuildMatrix (float *matrix, const aliashdr_t *hdr,
	const lerpdata_t *lerpdata, unsigned char scale)
{
	float entityscale = ENTSCALE_DECODE (scale);

	AliasMatrixIdentity (matrix);
	AliasMatrixPostTranslate (matrix, lerpdata->origin[0], lerpdata->origin[1], lerpdata->origin[2]);
	AliasMatrixPostRotate (matrix, lerpdata->angles[1], 0.0f, 0.0f, 1.0f);
	AliasMatrixPostRotate (matrix, -lerpdata->angles[0], 0.0f, 1.0f, 0.0f);
	AliasMatrixPostRotate (matrix, lerpdata->angles[2], 1.0f, 0.0f, 0.0f);
	AliasMatrixPostScale (matrix, entityscale, entityscale, entityscale);
	AliasMatrixPostTranslate (matrix, hdr->scale_origin[0], hdr->scale_origin[1], hdr->scale_origin[2]);
	AliasMatrixPostScale (matrix, hdr->scale[0], hdr->scale[1], hdr->scale[2]);
}

static qboolean GL_AliasInstanced_EnsureBuffer (void)
{
	if (r_alias_instance_vbo != 0)
		return true;
	if (!GL_GenBuffersFunc)
		return false;
	GL_GenBuffersFunc (1, &r_alias_instance_vbo);
	return r_alias_instance_vbo != 0;
}

static qboolean GL_AliasInstanced_Available (void)
{
	return r_alias_batch_scope && r_alias_batching.value && r_alias_instanced_program != 0 &&
		gl_caps.instancing != gl_capability_unavailable && gl_vbo_able &&
		GL_DrawElementsInstancedFunc && GL_VertexAttribDivisorFunc &&
		GL_BindBufferFunc && GL_BufferDataFunc && GL_BufferSubDataFunc &&
		GL_GenBuffersFunc && GL_DeleteBuffersFunc;
}

static qboolean GL_AliasInstanced_KeyMatches (qmodel_t *model, aliashdr_t *hdr,
	gltexture_t *texture, const lerpdata_t *lerpdata)
{
	return r_alias_instance_model == model && r_alias_instance_hdr == hdr &&
		r_alias_instance_texture == texture &&
		r_alias_instance_pose1 == lerpdata->pose1 &&
		r_alias_instance_pose2 == lerpdata->pose2 &&
		r_alias_instance_overbright == overbright;
}

static void GL_AliasInstanced_Flush (void)
{
	int i;
	const size_t stride = sizeof (r_alias_instances[0]);
	const size_t upload_size = r_alias_instance_count * stride;
	GLuint instance_buffer;
	GLintptr instance_offset;

	if (r_alias_instance_count == 0)
		return;

	GL_UseProgram (r_alias_instanced_program);
	GL_BindBuffer (GL_ARRAY_BUFFER, r_alias_instance_model->meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, r_alias_instance_model->meshindexesvbo);

	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2NormalAttrIndex);
	GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0,
		(void *)(intptr_t)r_alias_instance_model->vbostofs);
	GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE,
		sizeof (meshxyz_t), GLARB_GetXYZOffsetForModel (r_alias_instance_model, r_alias_instance_hdr, r_alias_instance_pose1));
	GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE,
		sizeof (meshxyz_t), GLARB_GetXYZOffsetForModel (r_alias_instance_model, r_alias_instance_hdr, r_alias_instance_pose2));
	GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE,
		sizeof (meshxyz_t), GLARB_GetNormalOffsetForModel (r_alias_instance_model, r_alias_instance_hdr, r_alias_instance_pose1));
	GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE,
		sizeof (meshxyz_t), GLARB_GetNormalOffsetForModel (r_alias_instance_model, r_alias_instance_hdr, r_alias_instance_pose2));

	if (!GL_FrameResources_UploadAliasInstances (r_alias_instances, upload_size,
		&instance_buffer, &instance_offset))
	{
		/* The fixed frame ring never overwrites an in-flight range.  Its rare
		 * overflow path retains the previous orphan/subdata upload behavior. */
		instance_buffer = r_alias_instance_vbo;
		instance_offset = 0;
		GL_BindBuffer (GL_ARRAY_BUFFER, instance_buffer);
		GL_BufferDataFunc (GL_ARRAY_BUFFER, upload_size, NULL, GL_STREAM_DRAW);
		GL_BufferSubDataFunc (GL_ARRAY_BUFFER, 0, upload_size, r_alias_instances);
		GL_FrameResources_RecordFallbackUpload (upload_size);
	}
	else
		GL_BindBuffer (GL_ARRAY_BUFFER, instance_buffer);
	for (i = 0; i < 4; i++)
	{
		GLuint attrib = instanceMatrix0AttrIndex + i;
		GL_EnableVertexAttribArrayFunc (attrib);
		GL_VertexAttribPointerFunc (attrib, 4, GL_FLOAT, GL_FALSE, stride,
			(void *)(intptr_t)(instance_offset + offsetof (alias_instance_t, matrix) + i * 4 * sizeof (float)));
		GL_VertexAttribDivisorFunc (attrib, 1);
	}
	GL_EnableVertexAttribArrayFunc (instanceShadeBlendAttrIndex);
	GL_VertexAttribPointerFunc (instanceShadeBlendAttrIndex, 4, GL_FLOAT, GL_FALSE,
		stride, (void *)(intptr_t)(instance_offset + offsetof (alias_instance_t, shadeblend)));
	GL_VertexAttribDivisorFunc (instanceShadeBlendAttrIndex, 1);
	GL_EnableVertexAttribArrayFunc (instanceLightAlphaAttrIndex);
	GL_VertexAttribPointerFunc (instanceLightAlphaAttrIndex, 4, GL_FLOAT, GL_FALSE,
		stride, (void *)(intptr_t)(instance_offset + offsetof (alias_instance_t, lightalpha)));
	GL_VertexAttribDivisorFunc (instanceLightAlphaAttrIndex, 1);

	GL_Uniform1iFunc (instancedTexLoc, 0);
	GL_Uniform1iFunc (instancedUseOverbrightLoc, r_alias_instance_overbright);
	GL_SelectTexture (GL_TEXTURE0);
	GL_Bind (r_alias_instance_texture);

	if (r_perfdebug.value)
	{
		r_perf_alias_glsl_draws++;
		r_perf_alias_batch_flushes++;
		r_perf_alias_instanced_draws++;
	}
	GL_DrawElementsInstancedFunc (GL_TRIANGLES, r_alias_instance_hdr->numindexes,
		GL_UNSIGNED_SHORT, (void *)(intptr_t)r_alias_instance_model->vboindexofs,
		r_alias_instance_count);
	rs_aliaspasses += r_alias_instance_hdr->numtris * r_alias_instance_count;

	for (i = 0; i < 4; i++)
	{
		GLuint attrib = instanceMatrix0AttrIndex + i;
		GL_VertexAttribDivisorFunc (attrib, 0);
		GL_DisableVertexAttribArrayFunc (attrib);
	}
	GL_VertexAttribDivisorFunc (instanceShadeBlendAttrIndex, 0);
	GL_VertexAttribDivisorFunc (instanceLightAlphaAttrIndex, 0);
	GL_DisableVertexAttribArrayFunc (instanceShadeBlendAttrIndex);
	GL_DisableVertexAttribArrayFunc (instanceLightAlphaAttrIndex);
	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2NormalAttrIndex);
	GL_UseProgram (0);
	GL_SelectTexture (GL_TEXTURE0);

	r_alias_instance_count = 0;
	r_alias_instance_model = NULL;
	r_alias_instance_hdr = NULL;
	r_alias_instance_texture = NULL;
}

static void GL_AliasInstanced_Queue (entity_t *e, aliashdr_t *hdr,
	const lerpdata_t *lerpdata, gltexture_t *texture)
{
	alias_instance_t *instance;

	if (r_alias_glsl_batch_active)
		GL_AliasBatch_End ();
	if (r_alias_instance_count && !GL_AliasInstanced_KeyMatches (e->model, hdr, texture, lerpdata))
		GL_AliasInstanced_Flush ();
	if (r_alias_instance_count == MAX_ALIAS_INSTANCES)
		GL_AliasInstanced_Flush ();
	if (r_alias_instance_count == 0)
	{
		r_alias_instance_model = e->model;
		r_alias_instance_hdr = hdr;
		r_alias_instance_texture = texture;
		r_alias_instance_pose1 = lerpdata->pose1;
		r_alias_instance_pose2 = lerpdata->pose2;
		r_alias_instance_overbright = overbright;
	}

	instance = &r_alias_instances[r_alias_instance_count++];
	AliasInstanceBuildMatrix (instance->matrix, hdr, lerpdata, e->scale);
	instance->shadeblend[0] = shadevector[0];
	instance->shadeblend[1] = shadevector[1];
	instance->shadeblend[2] = shadevector[2];
	instance->shadeblend[3] = lerpdata->blend;
	instance->lightalpha[0] = lightcolor[0];
	instance->lightalpha[1] = lightcolor[1];
	instance->lightalpha[2] = lightcolor[2];
	instance->lightalpha[3] = entalpha;
	if (r_perfdebug.value)
		r_perf_alias_instanced_submits++;
}

void GLAlias_DeleteInstanceBuffer (void)
{
	if (r_alias_instance_vbo != 0 && GL_DeleteBuffersFunc)
		GL_DeleteBuffersFunc (1, &r_alias_instance_vbo);
	r_alias_instance_vbo = 0;
	r_alias_instance_count = 0;
	r_alias_instance_model = NULL;
	r_alias_instance_hdr = NULL;
	r_alias_instance_texture = NULL;
}

void R_BeginAliasBatchScope (void)
{
	if (!r_alias_batching.value)
		return;
	r_alias_batch_scope = true;
}

void R_EndAliasBatchScope (void)
{
	GL_AliasBatch_End ();
	r_alias_batch_scope = false;
}

static void GL_AliasBatch_Begin (void)
{
	if (r_alias_glsl_batch_active && r_alias_glsl_batch_model == currententity->model)
		return;

	GL_AliasBatch_End ();

	GL_UseProgram (r_alias_program);

	GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, currententity->model->meshindexesvbo);

	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2NormalAttrIndex);

	GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0, (void *)(intptr_t)currententity->model->vbostofs);

	GL_Uniform1iFunc (texLoc, 0);
	GL_Uniform1iFunc (fullbrightTexLoc, 1);

	r_alias_glsl_batch_active = true;
	r_alias_glsl_batch_model = currententity->model;
}

/*
=============
GLAlias_CreateShaders
=============
*/
void GLAlias_CreateShaders (void)
{
	const glsl_attrib_binding_t bindings[] = {
		{ "TexCoords", texCoordsAttrIndex },
		{ "Pose1Vert", pose1VertexAttrIndex },
		{ "Pose1Normal", pose1NormalAttrIndex },
		{ "Pose2Vert", pose2VertexAttrIndex },
		{ "Pose2Normal", pose2NormalAttrIndex }
	};

	const GLchar *vertSource = \
		"#version 110\n"
		"\n"
		"uniform float Blend;\n"
		"uniform vec3 ShadeVector;\n"
		"uniform vec4 LightColor;\n"
		"attribute vec4 TexCoords; // only xy are used \n"
		"attribute vec4 Pose1Vert;\n"
		"attribute vec3 Pose1Normal;\n"
		"attribute vec4 Pose2Vert;\n"
		"attribute vec3 Pose2Normal;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"\n"
		"float r_avertexnormal_dot(vec3 vertexnormal) // from MH \n"
		"{\n"
		"        float dot = dot(vertexnormal, ShadeVector);\n"
		"        // wtf - this reproduces anorm_dots within as reasonable a degree of tolerance as the >= 0 case\n"
		"        if (dot < 0.0)\n"
		"            return 1.0 + dot * (13.0 / 44.0);\n"
		"        else\n"
		"            return 1.0 + dot;\n"
		"}\n"
		"void main()\n"
		"{\n"
		"	gl_TexCoord[0] = TexCoords;\n"
		"	vec4 lerpedVert = mix(vec4(Pose1Vert.xyz, 1.0), vec4(Pose2Vert.xyz, 1.0), Blend);\n"
		"	gl_Position = gl_ModelViewProjectionMatrix * lerpedVert;\n"
		"	FogFragCoord = gl_Position.w;\n"
		"	float dot1 = r_avertexnormal_dot(Pose1Normal);\n"
		"	float dot2 = r_avertexnormal_dot(Pose2Normal);\n"
		"	gl_FrontColor = LightColor * vec4(vec3(mix(dot1, dot2, Blend)), 1.0);\n"
		"}\n";

	const GLchar *fragSource = \
		"#version 110\n"
		"\n"
		"uniform sampler2D Tex;\n"
		"uniform sampler2D FullbrightTex;\n"
		"uniform bool UseFullbrightTex;\n"
		"uniform bool UseOverbright;\n"
		"uniform bool UseAlphaTest;\n"
		"\n"
		"varying float FogFragCoord;\n"
		"\n"
		"void main()\n"
		"{\n"
		"	vec4 result = texture2D(Tex, gl_TexCoord[0].xy);\n"
		"	if (UseAlphaTest && (result.a < 0.666))\n"
		"		discard;\n"
		"	result *= gl_Color;\n"
		"	if (UseOverbright)\n"
		"		result.rgb *= 2.0;\n"
		"	if (UseFullbrightTex)\n"
		"		result += texture2D(FullbrightTex, gl_TexCoord[0].xy);\n"
		"	result = clamp(result, 0.0, 1.0);\n"
		"	float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
		"	fog = clamp(fog, 0.0, 1.0);\n"
		"	result = mix(gl_Fog.color, result, fog);\n"
		"	result.a = gl_Color.a;\n" // FIXME: This will make almost transparent things cut holes though heavy fog
		"	gl_FragColor = result;\n"
		"}\n";

	r_alias_instanced_program = 0;
	if (!gl_glsl_alias_able)
		return;

	r_alias_program = GL_CreateProgram (vertSource, fragSource, Q_COUNTOF(bindings), bindings);

	if (r_alias_program != 0)
	{
	// get uniform locations
		blendLoc = GL_GetUniformLocation (&r_alias_program, "Blend");
		shadevectorLoc = GL_GetUniformLocation (&r_alias_program, "ShadeVector");
		lightColorLoc = GL_GetUniformLocation (&r_alias_program, "LightColor");
		texLoc = GL_GetUniformLocation (&r_alias_program, "Tex");
		fullbrightTexLoc = GL_GetUniformLocation (&r_alias_program, "FullbrightTex");
		useFullbrightTexLoc = GL_GetUniformLocation (&r_alias_program, "UseFullbrightTex");
		useOverbrightLoc = GL_GetUniformLocation (&r_alias_program, "UseOverbright");
		useAlphaTestLoc = GL_GetUniformLocation (&r_alias_program, "UseAlphaTest");
	}

	{
		const glsl_attrib_binding_t instancedBindings[] = {
			{ "TexCoords", texCoordsAttrIndex },
			{ "Pose1Vert", pose1VertexAttrIndex },
			{ "Pose1Normal", pose1NormalAttrIndex },
			{ "Pose2Vert", pose2VertexAttrIndex },
			{ "Pose2Normal", pose2NormalAttrIndex },
			{ "InstanceMatrix0", instanceMatrix0AttrIndex },
			{ "InstanceMatrix1", instanceMatrix1AttrIndex },
			{ "InstanceMatrix2", instanceMatrix2AttrIndex },
			{ "InstanceMatrix3", instanceMatrix3AttrIndex },
			{ "InstanceShadeBlend", instanceShadeBlendAttrIndex },
			{ "InstanceLightAlpha", instanceLightAlphaAttrIndex }
		};
		const GLchar *instancedVertSource = \
			"#version 110\n"
			"attribute vec4 TexCoords;\n"
			"attribute vec4 Pose1Vert;\n"
			"attribute vec3 Pose1Normal;\n"
			"attribute vec4 Pose2Vert;\n"
			"attribute vec3 Pose2Normal;\n"
			"attribute vec4 InstanceMatrix0;\n"
			"attribute vec4 InstanceMatrix1;\n"
			"attribute vec4 InstanceMatrix2;\n"
			"attribute vec4 InstanceMatrix3;\n"
			"attribute vec4 InstanceShadeBlend;\n"
			"attribute vec4 InstanceLightAlpha;\n"
			"varying float FogFragCoord;\n"
			"float shadedot(vec3 normal, vec3 shadevector)\n"
			"{\n"
			"  float d = dot(normal, shadevector);\n"
			"  return d < 0.0 ? 1.0 + d * (13.0 / 44.0) : 1.0 + d;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"  vec4 vertex = mix(vec4(Pose1Vert.xyz, 1.0), vec4(Pose2Vert.xyz, 1.0), InstanceShadeBlend.w);\n"
			"  gl_TexCoord[0] = TexCoords;\n"
			"  gl_Position = gl_ModelViewProjectionMatrix * mat4(InstanceMatrix0, InstanceMatrix1, InstanceMatrix2, InstanceMatrix3) * vertex;\n"
			"  FogFragCoord = gl_Position.w;\n"
			"  gl_FrontColor = InstanceLightAlpha * vec4(vec3(mix(shadedot(Pose1Normal, InstanceShadeBlend.xyz), shadedot(Pose2Normal, InstanceShadeBlend.xyz), InstanceShadeBlend.w)), 1.0);\n"
			"}\n";
		const GLchar *instancedFragSource = \
			"#version 110\n"
			"uniform sampler2D Tex;\n"
			"uniform bool UseOverbright;\n"
			"varying float FogFragCoord;\n"
			"void main()\n"
			"{\n"
			"  vec4 result = texture2D(Tex, gl_TexCoord[0].xy) * gl_Color;\n"
			"  if (UseOverbright) result.rgb *= 2.0;\n"
			"  result = clamp(result, 0.0, 1.0);\n"
			"  float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
			"  result.rgb = mix(gl_Fog.color.rgb, result.rgb, clamp(fog, 0.0, 1.0));\n"
			"  gl_FragColor = result;\n"
			"}\n";

		r_alias_instanced_program = GL_CreateProgram (instancedVertSource,
			instancedFragSource, Q_COUNTOF(instancedBindings), instancedBindings);
		if (r_alias_instanced_program != 0)
		{
			instancedTexLoc = GL_GetUniformLocation (&r_alias_instanced_program, "Tex");
			instancedUseOverbrightLoc = GL_GetUniformLocation (&r_alias_instanced_program, "UseOverbright");
		}
	}

	/*
	 * MD3 pose data is signed 1/64th-unit XYZ plus two packed normal angles,
	 * so it cannot use the MDL alias program's byte-normal vertex format.
	 */
	{
		const GLchar *md3VertSource = \
			"#version 110\n"
			"uniform float Blend;\n"
			"uniform vec3 ShadeVector;\n"
			"uniform vec4 LightColor;\n"
			"uniform bool UseShading;\n"
			"attribute vec4 TexCoords;\n"
			"attribute vec3 Pose1Vert;\n"
			"attribute vec2 Pose1Normal;\n"
			"attribute vec3 Pose2Vert;\n"
			"attribute vec2 Pose2Normal;\n"
			"varying float FogFragCoord;\n"
			"vec3 md3normal(vec2 latlong)\n"
			"{\n"
			"  float lat = latlong.x * (6.28318530718 / 255.0);\n"
			"  float lng = latlong.y * (6.28318530718 / 255.0);\n"
			"  return vec3(cos(lng) * sin(lat), sin(lng) * sin(lat), cos(lat));\n"
			"}\n"
			"float shadedot(vec3 normal)\n"
			"{\n"
			"  float d = dot(normal, ShadeVector);\n"
			"  return d < 0.0 ? 1.0 + d * (13.0 / 44.0) : 1.0 + d;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"  gl_TexCoord[0] = TexCoords;\n"
			"  vec4 vertex = vec4(mix(Pose1Vert, Pose2Vert, Blend), 1.0);\n"
			"  gl_Position = gl_ModelViewProjectionMatrix * vertex;\n"
			"  FogFragCoord = gl_Position.w;\n"
			"  if (UseShading)\n"
			"    gl_FrontColor = LightColor * vec4(vec3(shadedot(normalize(mix(md3normal(Pose1Normal), md3normal(Pose2Normal), Blend)))), 1.0);\n"
			"  else\n"
			"    gl_FrontColor = LightColor;\n"
			"}\n";
		const GLchar *md3FragSource = \
			"#version 110\n"
			"uniform sampler2D Tex;\n"
			"uniform bool UseTexture;\n"
			"uniform bool UseOverbright;\n"
			"uniform bool UseAlphaTest;\n"
			"uniform bool UseFog;\n"
			"varying float FogFragCoord;\n"
			"void main()\n"
			"{\n"
			"  vec4 result;\n"
			"  if (UseTexture)\n"
			"  {\n"
			"    result = texture2D(Tex, gl_TexCoord[0].xy);\n"
			"    if (UseAlphaTest && result.a * gl_Color.a < 0.666) discard;\n"
			"  }\n"
			"  else\n"
			"    result = vec4(1.0);\n"
			"  result *= gl_Color;\n"
			"  if (UseOverbright) result.rgb *= 2.0;\n"
			"  result = clamp(result, 0.0, 1.0);\n"
			"  if (UseFog)\n"
			"  {\n"
			"    float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
			"    result.rgb = mix(gl_Fog.color.rgb, result.rgb, clamp(fog, 0.0, 1.0));\n"
			"  }\n"
			"  gl_FragColor = result;\n"
			"}\n";

		r_md3_program = GL_CreateProgram (md3VertSource, md3FragSource,
			Q_COUNTOF(bindings), bindings);
		if (r_md3_program != 0)
		{
			md3BlendLoc = GL_GetUniformLocation (&r_md3_program, "Blend");
			md3ShadevectorLoc = GL_GetUniformLocation (&r_md3_program, "ShadeVector");
			md3LightColorLoc = GL_GetUniformLocation (&r_md3_program, "LightColor");
			md3TexLoc = GL_GetUniformLocation (&r_md3_program, "Tex");
			md3UseOverbrightLoc = GL_GetUniformLocation (&r_md3_program, "UseOverbright");
			md3UseAlphaTestLoc = GL_GetUniformLocation (&r_md3_program, "UseAlphaTest");
			md3UseShadingLoc = GL_GetUniformLocation (&r_md3_program, "UseShading");
			md3UseTextureLoc = GL_GetUniformLocation (&r_md3_program, "UseTexture");
			md3UseFogLoc = GL_GetUniformLocation (&r_md3_program, "UseFog");
		}
	}

	/* MD5 poses use full float positions and normals, unlike MD3's packed data. */
	{
		const GLchar *md5VertSource = \
			"#version 110\n"
			"uniform float Blend;\n"
			"uniform vec3 ShadeVector;\n"
			"uniform vec4 LightColor;\n"
			"uniform bool UseShading;\n"
			"uniform float TexSScale;\n"
			"uniform float TexTScale;\n"
			"attribute vec4 TexCoords;\n"
			"attribute vec3 Pose1Vert;\n"
			"attribute vec3 Pose1Normal;\n"
			"attribute vec3 Pose2Vert;\n"
			"attribute vec3 Pose2Normal;\n"
			"varying float FogFragCoord;\n"
			"float shadedot(vec3 normal)\n"
			"{\n"
			"  float d = dot(normal, ShadeVector);\n"
			"  return d < 0.0 ? 1.0 + d * (13.0 / 44.0) : 1.0 + d;\n"
			"}\n"
			"void main()\n"
			"{\n"
			"  gl_TexCoord[0] = vec4(TexCoords.xy * vec2(TexSScale, TexTScale), TexCoords.zw);\n"
			"  vec4 vertex = vec4(mix(Pose1Vert, Pose2Vert, Blend), 1.0);\n"
			"  gl_Position = gl_ModelViewProjectionMatrix * vertex;\n"
			"  FogFragCoord = gl_Position.w;\n"
			"  if (UseShading)\n"
			"    gl_FrontColor = LightColor * vec4(vec3(shadedot(normalize(mix(Pose1Normal, Pose2Normal, Blend)))), 1.0);\n"
			"  else\n"
			"    gl_FrontColor = LightColor;\n"
			"}\n";
		const GLchar *md5FragSource = \
			"#version 110\n"
			"uniform sampler2D Tex;\n"
			"uniform bool UseTexture;\n"
			"uniform bool UseAlphaTest;\n"
			"uniform bool UseFog;\n"
			"varying float FogFragCoord;\n"
			"void main()\n"
			"{\n"
			"  vec4 result;\n"
			"  if (UseTexture)\n"
			"  {\n"
			"    result = texture2D(Tex, gl_TexCoord[0].xy);\n"
			"    if (UseAlphaTest && result.a * gl_Color.a < 0.666) discard;\n"
			"  }\n"
			"  else\n"
			"    result = vec4(1.0);\n"
			"  result *= gl_Color;\n"
			"  result = clamp(result, 0.0, 1.0);\n"
			"  if (UseFog)\n"
			"  {\n"
			"    float fog = exp(-gl_Fog.density * gl_Fog.density * FogFragCoord * FogFragCoord);\n"
			"    result.rgb = mix(gl_Fog.color.rgb, result.rgb, clamp(fog, 0.0, 1.0));\n"
			"  }\n"
			"  gl_FragColor = result;\n"
			"}\n";

		r_md5_program = GL_CreateProgram (md5VertSource, md5FragSource,
			Q_COUNTOF(bindings), bindings);
		if (r_md5_program != 0)
		{
			md5BlendLoc = GL_GetUniformLocation (&r_md5_program, "Blend");
			md5ShadevectorLoc = GL_GetUniformLocation (&r_md5_program, "ShadeVector");
			md5LightColorLoc = GL_GetUniformLocation (&r_md5_program, "LightColor");
			md5TexLoc = GL_GetUniformLocation (&r_md5_program, "Tex");
			md5TexSScaleLoc = GL_GetUniformLocation (&r_md5_program, "TexSScale");
			md5TexTScaleLoc = GL_GetUniformLocation (&r_md5_program, "TexTScale");
			md5UseAlphaTestLoc = GL_GetUniformLocation (&r_md5_program, "UseAlphaTest");
			md5UseShadingLoc = GL_GetUniformLocation (&r_md5_program, "UseShading");
			md5UseTextureLoc = GL_GetUniformLocation (&r_md5_program, "UseTexture");
			md5UseFogLoc = GL_GetUniformLocation (&r_md5_program, "UseFog");
		}
	}
}

/*
=============
GL_DrawAliasFrame_GLSL -- ericw

Optimized alias model drawing codepath.
Compared to the original GL_DrawAliasFrame, this makes 1 draw call,
no vertex data is uploaded (it's already in the r_meshvbo and r_meshindexesvbo
static VBOs), and lerping and lighting is done in the vertex shader.

Supports optional overbright, optional fullbright pixels.

Based on code by MH from RMQEngine
=============
*/
void GL_DrawAliasFrame_GLSL (aliashdr_t *paliashdr, lerpdata_t lerpdata, gltexture_t *tx, gltexture_t *fb)
{
	float	blend;

	if (lerpdata.pose1 != lerpdata.pose2)
	{
		blend = lerpdata.blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		blend = 0;
	}

	if (r_alias_batch_scope && r_alias_batching.value)
	{
		GL_AliasBatch_Begin ();

		GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (paliashdr, lerpdata.pose2));
	// GL_TRUE to normalize the signed bytes to [-1 .. 1]
		GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (paliashdr, lerpdata.pose1));
		GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (paliashdr, lerpdata.pose2));

	// set uniforms
		GL_Uniform1fFunc (blendLoc, blend);
		GL_Uniform3fFunc (shadevectorLoc, shadevector[0], shadevector[1], shadevector[2]);
		GL_Uniform4fFunc (lightColorLoc, lightcolor[0], lightcolor[1], lightcolor[2], entalpha);
		GL_Uniform1iFunc (useFullbrightTexLoc, (fb != NULL) ? 1 : 0);
		GL_Uniform1fFunc (useOverbrightLoc, overbright);
		GL_Uniform1iFunc (useAlphaTestLoc, (currententity->model->flags & MF_HOLEY) ? 1 : 0);

	// set textures
		GL_SelectTexture (GL_TEXTURE0);
		GL_Bind (tx);

		if (fb)
		{
			GL_SelectTexture (GL_TEXTURE1);
			GL_Bind (fb);
		}

	// draw
		if (r_perfdebug.value)
			r_perf_alias_glsl_draws++;
		glDrawElements (GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT, (void *)(intptr_t)currententity->model->vboindexofs);

		rs_aliaspasses += paliashdr->numtris;
		return;
	}

	GL_UseProgram (r_alias_program);

	GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, currententity->model->meshindexesvbo);

	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2NormalAttrIndex);

	GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE, 0, (void *)(intptr_t)currententity->model->vbostofs);
	GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (paliashdr, lerpdata.pose1));
	GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (paliashdr, lerpdata.pose2));
// GL_TRUE to normalize the signed bytes to [-1 .. 1]
	GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (paliashdr, lerpdata.pose1));
	GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (paliashdr, lerpdata.pose2));

// set uniforms
	GL_Uniform1fFunc (blendLoc, blend);
	GL_Uniform3fFunc (shadevectorLoc, shadevector[0], shadevector[1], shadevector[2]);
	GL_Uniform4fFunc (lightColorLoc, lightcolor[0], lightcolor[1], lightcolor[2], entalpha);
	GL_Uniform1iFunc (texLoc, 0);
	GL_Uniform1iFunc (fullbrightTexLoc, 1);
	GL_Uniform1iFunc (useFullbrightTexLoc, (fb != NULL) ? 1 : 0);
	GL_Uniform1fFunc (useOverbrightLoc, overbright);
	GL_Uniform1iFunc (useAlphaTestLoc, (currententity->model->flags & MF_HOLEY) ? 1 : 0);

// set textures
	GL_SelectTexture (GL_TEXTURE0);
	GL_Bind (tx);

	if (fb)
	{
		GL_SelectTexture (GL_TEXTURE1);
		GL_Bind (fb);
	}

	// draw
	if (r_perfdebug.value)
		r_perf_alias_glsl_draws++;
	glDrawElements (GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT, (void *)(intptr_t)currententity->model->vboindexofs);

// clean up
	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2NormalAttrIndex);

	GL_UseProgram (0);
	GL_SelectTexture (GL_TEXTURE0);

	rs_aliaspasses += paliashdr->numtris;
}

/*
=============
GL_DrawAliasFrame -- johnfitz -- rewritten to support colored light, lerping, entalpha, multitexture, and r_drawflat
=============
*/
void GL_DrawAliasFrame (aliashdr_t *paliashdr, lerpdata_t lerpdata)
{
	float	vertcolor[4];
	trivertx_t *verts1, *verts2;
	int		*commands;
	int		count;
	float	u,v;
	float	blend, iblend;
	qboolean lerping;

	if (lerpdata.pose1 != lerpdata.pose2)
	{
		lerping = true;
		verts1  = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
		verts2  = verts1;
		verts1 += lerpdata.pose1 * paliashdr->poseverts;
		verts2 += lerpdata.pose2 * paliashdr->poseverts;
		blend = lerpdata.blend;
		iblend = 1.0f - blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		lerping = false;
		verts1  = (trivertx_t *)((byte *)paliashdr + paliashdr->posedata);
		verts2  = verts1; // avoid bogus compiler warning
		verts1 += lerpdata.pose1 * paliashdr->poseverts;
		blend = iblend = 0; // avoid bogus compiler warning
	}

	commands = (int *)((byte *)paliashdr + paliashdr->commands);

	vertcolor[3] = entalpha; //never changes, so there's no need to put this inside the loop

	while (1)
	{
		// get the vertex count and primitive type
		count = *commands++;
		if (!count)
			break;		// done

		if (count < 0)
		{
			count = -count;
			glBegin (GL_TRIANGLE_FAN);
		}
		else
			glBegin (GL_TRIANGLE_STRIP);

		do
		{
			u = ((float *)commands)[0];
			v = ((float *)commands)[1];
			if (mtexenabled)
			{
				GL_MTexCoord2fFunc (GL_TEXTURE0_ARB, u, v);
				GL_MTexCoord2fFunc (GL_TEXTURE1_ARB, u, v);
			}
			else
				glTexCoord2f (u, v);

			commands += 2;

			if (shading)
			{
				if (r_drawflat_cheatsafe)
				{
					srand(count * (unsigned int)(src_offset_t)commands);
					glColor3f (rand()%256/255.0, rand()%256/255.0, rand()%256/255.0);
				}
				else if (lerping)
				{
					vertcolor[0] = (shadedots[verts1->lightnormalindex]*iblend + shadedots[verts2->lightnormalindex]*blend) * lightcolor[0];
					vertcolor[1] = (shadedots[verts1->lightnormalindex]*iblend + shadedots[verts2->lightnormalindex]*blend) * lightcolor[1];
					vertcolor[2] = (shadedots[verts1->lightnormalindex]*iblend + shadedots[verts2->lightnormalindex]*blend) * lightcolor[2];
					glColor4fv (vertcolor);
				}
				else
				{
					vertcolor[0] = shadedots[verts1->lightnormalindex] * lightcolor[0];
					vertcolor[1] = shadedots[verts1->lightnormalindex] * lightcolor[1];
					vertcolor[2] = shadedots[verts1->lightnormalindex] * lightcolor[2];
					glColor4fv (vertcolor);
				}
			}

			if (lerping)
			{
				glVertex3f (verts1->v[0]*iblend + verts2->v[0]*blend,
							verts1->v[1]*iblend + verts2->v[1]*blend,
							verts1->v[2]*iblend + verts2->v[2]*blend);
				verts1++;
				verts2++;
			}
			else
			{
				glVertex3f (verts1->v[0], verts1->v[1], verts1->v[2]);
				verts1++;
			}
		} while (--count);

		glEnd ();
	}

	rs_aliaspasses += paliashdr->numtris;
}

/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping
=================
*/
void R_SetupAliasFrame (aliashdr_t *paliashdr, int frame, lerpdata_t *lerpdata)
{
	entity_t		*e = currententity;
	int				posenum, numposes;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	posenum = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		e->lerptime = paliashdr->frames[frame].interval;
		posenum += (int)(cl.time / e->lerptime) % numposes;
	}
	else
		e->lerptime = 0.1;

	/*
	 * r_enhancedmodels can switch a paired MDL/MD3 at runtime.  The
	 * selection callback resets active entities, but retain this local guard
	 * for native models and any state restored between frames.
	 */
	if (e->currentpose < 0 || e->currentpose >= paliashdr->numposes ||
		e->previouspose < 0 || e->previouspose >= paliashdr->numposes)
		e->lerpflags |= LERP_RESETANIM;

	if (e->lerpflags & LERP_RESETANIM) //kill any lerp in progress
	{
		e->lerpstart = 0;
		e->previouspose = posenum;
		e->currentpose = posenum;
		e->lerpflags -= LERP_RESETANIM;
	}
	else if (e->currentpose != posenum) // pose changed, start new lerp
	{
		if (e->lerpflags & LERP_RESETANIM2) //defer lerping one more time
		{
			e->lerpstart = 0;
			e->previouspose = posenum;
			e->currentpose = posenum;
			e->lerpflags -= LERP_RESETANIM2;
		}
		else
		{
			e->lerpstart = cl.time;
			e->previouspose = e->currentpose;
			e->currentpose = posenum;
		}
	}

	//set up values
	if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
	{
		if (e->lerpflags & LERP_FINISH && numposes == 1)
			lerpdata->blend = CLAMP (0.0f, (float)(cl.time - e->lerpstart) / (e->lerpfinish - e->lerpstart), 1.0f);
		else
			lerpdata->blend = CLAMP (0.0f, (float)(cl.time - e->lerpstart) / e->lerptime, 1.0f);
		if (lerpdata->blend == 1.0f)
			e->previouspose = e->currentpose;
		lerpdata->pose1 = e->previouspose;
		lerpdata->pose2 = e->currentpose;
	}
	else //don't lerp
	{
		lerpdata->blend = 1;
		lerpdata->pose1 = posenum;
		lerpdata->pose2 = posenum;
	}
}

/*
=================
R_SetupEntityTransform -- johnfitz -- set up transform part of lerpdata
=================
*/
void R_SetupEntityTransform (entity_t *e, lerpdata_t *lerpdata)
{
	float blend;
	vec3_t d;
	int i;

	// if LERP_RESETMOVE, kill any lerps in progress
	if (e->lerpflags & LERP_RESETMOVE)
	{
		e->movelerpstart = 0;
		VectorCopy (e->origin, e->previousorigin);
		VectorCopy (e->origin, e->currentorigin);
		VectorCopy (e->angles, e->previousangles);
		VectorCopy (e->angles, e->currentangles);
		e->lerpflags -= LERP_RESETMOVE;
	}
	else if (!VectorCompare (e->origin, e->currentorigin) || !VectorCompare (e->angles, e->currentangles)) // origin/angles changed, start new lerp
	{
		e->movelerpstart = cl.time;
		VectorCopy (e->currentorigin, e->previousorigin);
		VectorCopy (e->origin,  e->currentorigin);
		VectorCopy (e->currentangles, e->previousangles);
		VectorCopy (e->angles,  e->currentangles);
	}

	//set up values
	if (r_lerpmove.value && e != &cl.viewent && e->lerpflags & LERP_MOVESTEP)
	{
		if (e->lerpflags & LERP_FINISH)
			blend = CLAMP (0.0f, (float)(cl.time - e->movelerpstart) / (e->lerpfinish - e->movelerpstart), 1.0f);
		else
			blend = CLAMP (0.0f, (float)(cl.time - e->movelerpstart) / 0.1f, 1.0f);

		//translation
		VectorSubtract (e->currentorigin, e->previousorigin, d);
		lerpdata->origin[0] = e->previousorigin[0] + d[0] * blend;
		lerpdata->origin[1] = e->previousorigin[1] + d[1] * blend;
		lerpdata->origin[2] = e->previousorigin[2] + d[2] * blend;

		//rotation
		VectorSubtract (e->currentangles, e->previousangles, d);
		for (i = 0; i < 3; i++)
		{
			if (d[i] > 180)  d[i] -= 360;
			if (d[i] < -180) d[i] += 360;
		}
		lerpdata->angles[0] = e->previousangles[0] + d[0] * blend;
		lerpdata->angles[1] = e->previousangles[1] + d[1] * blend;
		lerpdata->angles[2] = e->previousangles[2] + d[2] * blend;
	}
	else //don't lerp
	{
		VectorCopy (e->origin, lerpdata->origin);
		VectorCopy (e->angles, lerpdata->angles);
	}
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
void R_SetupAliasLighting (entity_t	*e)
{
	vec3_t		dist;
	float		add;
	int			i;
	int		quantizedangle;
	float		radiansangle;

	// if the initial trace is completely black, try again from above
	// this helps with models whose origin is slightly below ground level
	// (e.g. some of the candles in the DOTM start map)
	if (!R_LightPoint (e->origin))
	{
		vec3_t lpos;
		VectorCopy (e->origin, lpos);
		lpos[2] += e->model->maxs[2] * 0.5f;
		R_LightPoint (lpos);
	}

	//add dlights
	for (i=0 ; i<MAX_DLIGHTS ; i++)
	{
		if (cl_dlights[i].die >= cl.time)
		{
			VectorSubtract (currententity->origin, cl_dlights[i].origin, dist);
			add = cl_dlights[i].radius - VectorLength(dist);
			if (add > 0)
				VectorMA (lightcolor, add, cl_dlights[i].color, lightcolor);
		}
	}

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		add = 72.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (currententity > cl.entities && currententity <= cl.entities + cl.maxclients)
	{
		add = 24.0f - (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add > 0.0f)
		{
			lightcolor[0] += add / 3.0f;
			lightcolor[1] += add / 3.0f;
			lightcolor[2] += add / 3.0f;
		}
	}

	// clamp lighting so it doesn't overbright as much (96)
	if (overbright)
	{
		add = 288.0f / (lightcolor[0] + lightcolor[1] + lightcolor[2]);
		if (add < 1.0f)
			VectorScale(lightcolor, add, lightcolor);
	}

	//hack up the brightness when fullbrights but no overbrights (256)
	if (gl_fullbrights.value && !gl_overbright_models.value)
		if (e->model->flags & MOD_FBRIGHTHACK)
		{
			lightcolor[0] = 256.0f;
			lightcolor[1] = 256.0f;
			lightcolor[2] = 256.0f;
		}

	quantizedangle = ((int)(e->angles[1] * (SHADEDOT_QUANT / 360.0))) & (SHADEDOT_QUANT - 1);

//ericw -- shadevector is passed to the shader to compute shadedots inside the
//shader, see GLAlias_CreateShaders()
	radiansangle = (quantizedangle / 16.0) * 2.0 * 3.14159;
	shadevector[0] = cos(-radiansangle);
	shadevector[1] = sin(-radiansangle);
	shadevector[2] = 1;
	VectorNormalize(shadevector);
//ericw --

	shadedots = r_avertexnormal_dots[quantizedangle];
	VectorScale (lightcolor, 1.0f / 200.0f, lightcolor);
}

/*
==============================================================================

					MD3 DRAWING

MD3 textured passes use the uploaded pose VBO when the MD3 program is
available.  Diagnostic and untextured paths retain the fixed-function mesh
renderer; regular, overbright, and fullbright passes remain separate draws.
==============================================================================
*/

static aliashdr_t *R_NextMD3Surface (aliashdr_t *surface)
{
	return surface->nextsurface ?
		(aliashdr_t *)((byte *)surface + surface->nextsurface) : NULL;
}

static int R_MD3SurfaceSkin (const aliashdr_t *surface, int skinnum)
{
	if (skinnum < 0 || skinnum >= surface->numskins || !surface->gltextures[skinnum][0])
		return 0;
	return skinnum;
}

static qboolean R_MD3UsesAlpha (aliashdr_t *surface, int skinnum)
{
	while (surface)
	{
		gltexture_t *texture = surface->gltextures[R_MD3SurfaceSkin (surface, skinnum)][0];
		if (texture && (texture->flags & TEXPREF_ALPHA))
			return true;
		surface = R_NextMD3Surface (surface);
	}
	return false;
}

static void R_SetViewModelColor (float alpha)
{
	if (vr_weaponcolor[0] != 1.0f || vr_weaponcolor[1] != 1.0f ||
		vr_weaponcolor[2] != 1.0f)
		glColor4f (vr_weaponcolor[0], vr_weaponcolor[1], vr_weaponcolor[2], alpha);
	else
		glColor4f (1.0f, 1.0f, 1.0f, alpha);
}

static void R_MD3Normal (const md3vertex_t *vert, vec3_t normal)
{
	float lat = (float)vert->latlong[0] * (2.0f * (float)M_PI / 255.0f);
	float lng = (float)vert->latlong[1] * (2.0f * (float)M_PI / 255.0f);

	normal[0] = cosf (lng) * sinf (lat);
	normal[1] = sinf (lng) * sinf (lat);
	normal[2] = cosf (lat);
}

static void *GLMD3_GetXYZOffset (const aliashdr_t *surface, int surfaceindex, int pose)
{
	return (void *)(intptr_t)((size_t)currententity->model->md3vboxyzofs[surfaceindex] +
		(size_t)pose * surface->numverts * sizeof(md3vertex_t) + offsetof(md3vertex_t, xyz));
}

static void *GLMD3_GetNormalOffset (const aliashdr_t *surface, int surfaceindex, int pose)
{
	return (void *)(intptr_t)((size_t)currententity->model->md3vboxyzofs[surfaceindex] +
		(size_t)pose * surface->numverts * sizeof(md3vertex_t) + offsetof(md3vertex_t, latlong));
}

static void GL_DrawMD3Frame_GLSL (const aliashdr_t *surface, int surfaceindex,
	lerpdata_t lerpdata, gltexture_t *texture, const vec3_t drawcolor,
	float drawalpha, qboolean drawshading, qboolean usetexture, qboolean usefog)
{
	float blend = lerpdata.pose1 != lerpdata.pose2 ? lerpdata.blend : 0.0f;

	GL_UseProgram (r_md3_program);
	GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->md3meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, currententity->model->md3meshindexesvbo);

	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2NormalAttrIndex);

	GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE,
		sizeof(meshst_t), (void *)(intptr_t)currententity->model->md3vbostofs[surfaceindex]);
	GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 3, GL_SHORT, GL_FALSE,
		sizeof(md3vertex_t), GLMD3_GetXYZOffset(surface, surfaceindex, lerpdata.pose1));
	GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 3, GL_SHORT, GL_FALSE,
		sizeof(md3vertex_t), GLMD3_GetXYZOffset(surface, surfaceindex, lerpdata.pose2));
	GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 2, GL_UNSIGNED_BYTE, GL_FALSE,
		sizeof(md3vertex_t), GLMD3_GetNormalOffset(surface, surfaceindex, lerpdata.pose1));
	GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 2, GL_UNSIGNED_BYTE, GL_FALSE,
		sizeof(md3vertex_t), GLMD3_GetNormalOffset(surface, surfaceindex, lerpdata.pose2));

	GL_Uniform1fFunc (md3BlendLoc, blend);
	GL_Uniform3fFunc (md3ShadevectorLoc, shadevector[0], shadevector[1], shadevector[2]);
	GL_Uniform4fFunc (md3LightColorLoc, drawcolor[0], drawcolor[1], drawcolor[2], drawalpha);
	GL_Uniform1iFunc (md3TexLoc, 0);
	GL_Uniform1iFunc (md3UseTextureLoc, usetexture ? 1 : 0);
	/* Overbright is a second additive draw so Fog_StartAdditive can blacken fog. */
	GL_Uniform1iFunc (md3UseOverbrightLoc, 0);
	GL_Uniform1iFunc (md3UseAlphaTestLoc,
		(usetexture && r_md3_glsl_alphatest) ? 1 : 0);
	GL_Uniform1iFunc (md3UseShadingLoc, drawshading ? 1 : 0);
	GL_Uniform1iFunc (md3UseFogLoc, usefog ? 1 : 0);

	if (usetexture)
	{
		GL_SelectTexture (GL_TEXTURE0);
		GL_Bind (texture);
	}
	if (r_perfdebug.value)
		r_perf_alias_glsl_draws++;
	glDrawElements (GL_TRIANGLES, surface->numindexes, GL_UNSIGNED_SHORT,
		(void *)(intptr_t)currententity->model->md3vboindexofs[surfaceindex]);

	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2NormalAttrIndex);
	GL_UseProgram (0);
	GL_SelectTexture (GL_TEXTURE0);
	rs_aliaspasses += surface->numtris;
}

static void GL_DrawMD3Frame (aliashdr_t *surface, lerpdata_t lerpdata)
{
	const md3vertex_t *verts1, *verts2;
	const meshst_t *st;
	const unsigned short *indexes;
	float blend, iblend;
	int index, i;
	qboolean lerping;

	verts1 = (const md3vertex_t *)((const byte *)surface + surface->vertexes) +
		lerpdata.pose1 * surface->numverts;
	verts2 = (const md3vertex_t *)((const byte *)surface + surface->vertexes) +
		lerpdata.pose2 * surface->numverts;
	st = (const meshst_t *)((const byte *)surface + surface->meshdesc);
	indexes = (const unsigned short *)((const byte *)surface + surface->indexes);
	lerping = lerpdata.pose1 != lerpdata.pose2;
	blend = lerping ? lerpdata.blend : 0.0f;
	iblend = 1.0f - blend;

	glBegin (GL_TRIANGLES);
	for (i = 0; i < surface->numindexes; i++)
	{
		const md3vertex_t *v1, *v2;

		index = indexes[i];
		v1 = verts1 + index;
		v2 = verts2 + index;
		glTexCoord2fv (st[index].st);
		if (shading)
		{
			vec3_t normal;
			float dot;

			R_MD3Normal (v2, normal);
			if (lerping)
			{
				vec3_t previous;
				R_MD3Normal (v1, previous);
				normal[0] = previous[0] * iblend + normal[0] * blend;
				normal[1] = previous[1] * iblend + normal[1] * blend;
				normal[2] = previous[2] * iblend + normal[2] * blend;
				VectorNormalize (normal);
			}
			dot = DotProduct (normal, shadevector);
			if (dot < 0.0f)
				dot = 1.0f + dot * (13.0f / 44.0f);
			else
				dot = 1.0f + dot;
			glColor4f (dot * lightcolor[0], dot * lightcolor[1],
				dot * lightcolor[2], entalpha);
		}
		if (lerping)
			glVertex3f (v1->xyz[0] * iblend + v2->xyz[0] * blend,
				v1->xyz[1] * iblend + v2->xyz[1] * blend,
				v1->xyz[2] * iblend + v2->xyz[2] * blend);
		else
			glVertex3f (v2->xyz[0], v2->xyz[1], v2->xyz[2]);
	}
	glEnd ();

	rs_aliaspasses += surface->numtris;
}

static int R_MD3TriangleCount (aliashdr_t *surface)
{
	int tris = 0;

	while (surface)
	{
		tris += surface->numtris;
		surface = R_NextMD3Surface (surface);
	}
	return tris;
}

static void R_DrawMD3Pass (aliashdr_t *surface, lerpdata_t lerpdata,
	int skinnum, qboolean fullbright, const vec3_t drawcolor, float drawalpha,
	qboolean drawshading, qboolean drawfog)
{
	int surfaceindex = 0;

	while (surface)
	{
		int skin = R_MD3SurfaceSkin (surface, skinnum);
		gltexture_t *texture = fullbright ? surface->fbtextures[skin][0] :
			surface->gltextures[skin][0];

		if (texture || !fullbright)
		{
			if (r_md3_glsl_active)
				GL_DrawMD3Frame_GLSL (surface, surfaceindex, lerpdata, texture,
					drawcolor, drawalpha, drawshading, true, drawfog);
			else
			{
				GL_Bind (texture);
				GL_DrawMD3Frame (surface, lerpdata);
			}
		}
		surface = R_NextMD3Surface (surface);
		surfaceindex++;
	}
}

static void R_DrawMD3UntexturedPass (aliashdr_t *surface, lerpdata_t lerpdata,
	const vec3_t drawcolor, float drawalpha, qboolean drawfog)
{
	int surfaceindex = 0;
	qboolean useglsl = r_md3_program != 0 && currententity->model->md3meshvbo != 0 &&
		currententity->model->md3meshindexesvbo != 0;

	while (surface)
	{
		if (useglsl)
			/* UseTexture=false deliberately avoids sampling while the caller has
			 * GL_TEXTURE_2D disabled for a flat/debug/shadow pass. */
			GL_DrawMD3Frame_GLSL (surface, surfaceindex, lerpdata, NULL,
				drawcolor, drawalpha, false, false, drawfog);
		else
			GL_DrawMD3Frame (surface, lerpdata);
		surface = R_NextMD3Surface (surface);
		surfaceindex++;
	}
}

static void R_DrawMD3Model (entity_t *e, qboolean cull, qboolean viewmodel)
{
	aliashdr_t *md3;
	lerpdata_t lerpdata;
	vec3_t fullbrightcolor;
	vec3_t drawflatcolor = {1, 1, 1};
	int skinnum;
	qboolean alphatest = false;
	qboolean drawfog;
	float fovscale = 1.0f;

	md3 = Mod_GetMD3Extradata (e->model);
	if (!md3)
		return;

	if (r_perfdebug.value)
		r_perf_alias_draws++;

	R_SetupAliasFrame (md3, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	if (cull && R_CullModelForEntity(e))
	{
		if (r_perfdebug.value)
			r_perf_alias_culled++;
		return;
	}

	GL_AliasBatch_End ();
	if (!vr_enabled.value && e == &cl.viewent && scr_fov.value > 90.f && cl_gun_fovscale.value)
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (md3->scale_origin[0], md3->scale_origin[1] * fovscale,
		md3->scale_origin[2] * fovscale);
	glScalef (md3->scale[0], md3->scale[1] * fovscale, md3->scale[2] * fovscale);

	if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
		glShadeModel (GL_SMOOTH);
	if (gl_affinemodels.value)
		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	overbright = !!gl_overbright_models.value;
	shading = true;

	entalpha = (r_drawflat_cheatsafe || r_lightmap_cheatsafe) ? 1.0f : ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0.0f)
		goto cleanup;
	fullbrightcolor[0] = fullbrightcolor[1] = fullbrightcolor[2] = entalpha;
	if (entalpha < 1.0f)
	{
		overbright = false;
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
	}

	skinnum = e->skinnum;
	if (skinnum < 0 || skinnum >= md3->numskins)
		skinnum = 0;
	alphatest = R_MD3UsesAlpha (md3, skinnum);
	if (alphatest)
		glEnable (GL_ALPHA_TEST);

	rs_aliaspolys += R_MD3TriangleCount (md3);
	if (!viewmodel)
		R_SetupAliasLighting (e);
	drawfog = !viewmodel && Fog_GetDensity() > 0.0f;
	GL_DisableMultitexture ();
	/* Each legacy textured pass remains a separate GLSL draw: this retains
	 * additive black-fog composition for overbright and fullbright overlays. */
	r_md3_glsl_active = !r_drawflat_cheatsafe && !r_lightmap_cheatsafe &&
		r_md3_program != 0 && e->model->md3meshvbo != 0 &&
		e->model->md3meshindexesvbo != 0;
	r_md3_glsl_alphatest = r_md3_glsl_active && alphatest;
	if (r_md3_glsl_active && alphatest)
		glDisable (GL_ALPHA_TEST); /* shader tests texture alpha multiplied by entalpha */

	if (viewmodel)
	{
		overbright = false;
		shading = false;
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		R_SetViewModelColor (entalpha);
		R_DrawMD3Pass (md3, lerpdata, skinnum, false, vr_weaponcolor,
			entalpha, false, false);
		if (gl_fullbrights.value)
		{
			glEnable (GL_BLEND);
			glBlendFunc (GL_ONE, GL_ONE);
			glDepthMask (GL_FALSE);
			glColor4f (entalpha, entalpha, entalpha, entalpha);
			R_DrawMD3Pass (md3, lerpdata, skinnum, true,
				fullbrightcolor, entalpha, false, false);
			glDepthMask (GL_TRUE);
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
		}
	}
	else if (r_drawflat_cheatsafe || r_lightmap_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		shading = false;
		glColor4f (1, 1, 1, entalpha);
		R_DrawMD3UntexturedPass (md3, lerpdata, drawflatcolor, entalpha, drawfog);
		glEnable (GL_TEXTURE_2D);
	}
	else
	{
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		if (r_fullbright_cheatsafe)
		{
			shading = false;
			glColor4f (1, 1, 1, entalpha);
			R_DrawMD3Pass (md3, lerpdata, skinnum, false,
				drawflatcolor, entalpha, false, drawfog);
		}
		else
		{
			R_DrawMD3Pass (md3, lerpdata, skinnum, false, lightcolor,
				entalpha, true, drawfog);
			if (overbright)
			{
				glEnable (GL_BLEND);
				glBlendFunc (GL_ONE, GL_ONE);
				glDepthMask (GL_FALSE);
				Fog_StartAdditive ();
				R_DrawMD3Pass (md3, lerpdata, skinnum, false, lightcolor,
					entalpha, true, drawfog);
				Fog_StopAdditive ();
				glDepthMask (GL_TRUE);
				glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable (GL_BLEND);
			}
		}

		if (gl_fullbrights.value)
		{
			glEnable (GL_BLEND);
			glBlendFunc (GL_ONE, GL_ONE);
			glDepthMask (GL_FALSE);
			shading = false;
			glColor4f (entalpha, entalpha, entalpha, entalpha);
			Fog_StartAdditive ();
			R_DrawMD3Pass (md3, lerpdata, skinnum, true,
				fullbrightcolor, entalpha, false, drawfog);
			Fog_StopAdditive ();
			glDepthMask (GL_TRUE);
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
		}
	}

cleanup:
	r_md3_glsl_active = false;
	r_md3_glsl_alphatest = false;
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glShadeModel (GL_FLAT);
	glDepthMask (GL_TRUE);
	glDisable (GL_BLEND);
	if (alphatest)
		glDisable (GL_ALPHA_TEST);
	glColor3f (1, 1, 1);
	glPopMatrix ();
}

static float R_VRIKLerpAngle (float from, float to, float blend)
{
	float delta = to - from;

	while (delta > 180.0f) delta -= 360.0f;
	while (delta < -180.0f) delta += 360.0f;
	return from + delta * blend;
}

static qboolean R_VRIKSampleEntityPose (const entity_t *entity, vrik_pose_t *out)
{
	const vrik_pose_t *newest, *older;
	double newesttime, oldertime, sampletime;
	float blend;
	int tracker, axis;

	if (!entity || !out || entity->vrik_pose_count < 1)
		return false;
	newest = &entity->vrik_poses[0];
	newesttime = entity->vrik_pose_times[0];
	if (!(newest->flags & VRIK_FLAG_ACTIVE) ||
		!(newest->flags & VRIK_FLAG_HEAD_TRACKED) ||
		realtime - newesttime > VRIK_POSE_STALE_TIME)
		return false;

	*out = *newest;
	if (entity->vrik_pose_count < 2)
		return true;
	older = &entity->vrik_poses[1];
	oldertime = entity->vrik_pose_times[1];
	if (!(older->flags & VRIK_FLAG_ACTIVE) || newesttime <= oldertime)
		return true;

	/* A short interpolation delay absorbs the natural 20 Hz pose cadence. */
	sampletime = realtime - 0.075;
	blend = (float)((sampletime - oldertime) / (newesttime - oldertime));
	blend = CLAMP(0.0f, blend, 1.0f);
	for (tracker = 0; tracker < VRIK_TRACKER_COUNT; tracker++)
		for (axis = 0; axis < 3; axis++)
		{
			out->position[tracker][axis] = older->position[tracker][axis] +
				(newest->position[tracker][axis] - older->position[tracker][axis]) * blend;
			out->orientation[tracker][axis] = R_VRIKLerpAngle (
				older->orientation[tracker][axis], newest->orientation[tracker][axis], blend);
		}
	out->body_yaw = R_VRIKLerpAngle (older->body_yaw, newest->body_yaw, blend);
	return true;
}

static void R_VRIKMatrixOrigin (const float matrix[12], vec3_t origin)
{
	origin[0] = matrix[3];
	origin[1] = matrix[7];
	origin[2] = matrix[11];
}

static void R_VRIKSetMatrixOrigin (float matrix[12], const vec3_t origin)
{
	matrix[3] = origin[0];
	matrix[7] = origin[1];
	matrix[11] = origin[2];
}

static void R_VRIKMatrixMultiply (const float first[12], const float second[12],
	float out[12])
{
	float result[12];

	R_ConcatTransforms ((float (*)[4])first, (float (*)[4])second,
		(float (*)[4])result);
	memcpy (out, result, sizeof(result));
}

static void R_VRIKMatrixInverseRigid (const float matrix[12], float inverse[12])
{
	int row, column;

	for (row = 0; row < 3; row++)
		for (column = 0; column < 3; column++)
			inverse[row * 4 + column] = matrix[column * 4 + row];
	for (row = 0; row < 3; row++)
		inverse[row * 4 + 3] = -(inverse[row * 4 + 0] * matrix[3] +
			inverse[row * 4 + 1] * matrix[7] +
			inverse[row * 4 + 2] * matrix[11]);
}

static void R_VRIKOrthonormalize (float matrix[12])
{
	vec3_t x = {matrix[0], matrix[4], matrix[8]};
	vec3_t y = {matrix[1], matrix[5], matrix[9]};
	vec3_t z;
	float projection;

	if (!VectorNormalize (x))
		x[0] = 1, x[1] = 0, x[2] = 0;
	projection = DotProduct (x, y);
	VectorMA (y, -projection, x, y);
	if (!VectorNormalize (y))
	{
		y[0] = 0, y[1] = 1, y[2] = 0;
		projection = DotProduct (x, y);
		VectorMA (y, -projection, x, y);
		VectorNormalize (y);
	}
	CrossProduct (x, y, z);
	VectorNormalize (z);
	matrix[0] = x[0]; matrix[4] = x[1]; matrix[8] = x[2];
	matrix[1] = y[0]; matrix[5] = y[1]; matrix[9] = y[2];
	matrix[2] = z[0]; matrix[6] = z[1]; matrix[10] = z[2];
}

static void R_VRIKLerpPalette (const md5liveinfo_t *live,
	const lerpdata_t *lerpdata, float *palette)
{
	const float *first = live->boneposes +
		(size_t)lerpdata->pose1 * live->numbones * 12;
	const float *second = live->boneposes +
		(size_t)lerpdata->pose2 * live->numbones * 12;
	int joint, component;

	for (joint = 0; joint < live->numbones; joint++)
	{
		float *matrix = palette + joint * 12;
		for (component = 0; component < 12; component++)
			matrix[component] = first[joint * 12 + component] +
				(second[joint * 12 + component] - first[joint * 12 + component]) *
				lerpdata->blend;
		R_VRIKOrthonormalize (matrix);
	}
}

static qboolean R_VRIKBuildBodyBasis (const md5liveinfo_t *live,
	const float *palette, vec3_t lateral, vec3_t forward, vec3_t up)
{
	vec3_t leftshoulder, rightshoulder, hip, head;
	int left = live->jointindex[MD5_VRIK_SHOULDER_L];
	int right = live->jointindex[MD5_VRIK_SHOULDER_R];
	int hipjoint = live->jointindex[MD5_VRIK_HIP];
	int headjoint = live->jointindex[MD5_VRIK_HEAD];

	if (left < 0 || right < 0 || hipjoint < 0 || headjoint < 0)
		return false;
	R_VRIKMatrixOrigin (palette + left * 12, leftshoulder);
	R_VRIKMatrixOrigin (palette + right * 12, rightshoulder);
	R_VRIKMatrixOrigin (palette + hipjoint * 12, hip);
	R_VRIKMatrixOrigin (palette + headjoint * 12, head);
	VectorSubtract (rightshoulder, leftshoulder, lateral);
	VectorSubtract (head, hip, up);
	if (!VectorNormalize (lateral) || !VectorNormalize (up))
		return false;
	CrossProduct (lateral, up, forward);
	if (!VectorNormalize (forward))
		return false;
	CrossProduct (forward, lateral, up);
	VectorNormalize (up);
	return true;
}

static void R_VRIKLocalVectorToModel (const vec3_t local,
	const vec3_t lateral, const vec3_t forward, const vec3_t up, vec3_t model)
{
	/* Network local coordinates are Quake +X forward, +Y left, +Z up. */
	model[0] = forward[0] * local[0] - lateral[0] * local[1] + up[0] * local[2];
	model[1] = forward[1] * local[0] - lateral[1] * local[1] + up[1] * local[2];
	model[2] = forward[2] * local[0] - lateral[2] * local[1] + up[2] * local[2];
}

static void R_VRIKCanonicalMatrix (vec3_t direction,
	vec3_t preferred_up, vec3_t origin, float matrix[12])
{
	vec3_t x, y, z;

	VectorCopy (direction, x);
	if (!VectorNormalize (x))
		x[0] = 1, x[1] = 0, x[2] = 0;
	CrossProduct (x, preferred_up, y); /* local +Y is left */
	if (!VectorNormalize (y))
	{
		vec3_t fallback = {0, 0, 1};
		CrossProduct (x, fallback, y);
		if (!VectorNormalize (y))
			y[0] = 0, y[1] = 1, y[2] = 0;
	}
	CrossProduct (y, x, z);
	VectorNormalize (z);
	matrix[0] = x[0]; matrix[1] = y[0]; matrix[2] = z[0]; matrix[3] = origin[0];
	matrix[4] = x[1]; matrix[5] = y[1]; matrix[6] = z[1]; matrix[7] = origin[1];
	matrix[8] = x[2]; matrix[9] = y[2]; matrix[10] = z[2]; matrix[11] = origin[2];
}

static void R_VRIKAnglesToModelMatrix (const vec3_t angles,
	const vec3_t lateral, const vec3_t forward, const vec3_t up,
	const vec3_t origin, float matrix[12])
{
	vec3_t quakeforward, quakeright, quakeup;
	vec3_t x, y, z, quakeleft;

	AngleVectors ((float *)angles, quakeforward, quakeright, quakeup);
	quakeleft[0] = -quakeright[0];
	quakeleft[1] = -quakeright[1];
	quakeleft[2] = -quakeright[2];
	R_VRIKLocalVectorToModel (quakeforward, lateral, forward, up, x);
	R_VRIKLocalVectorToModel (quakeleft, lateral, forward, up, y);
	R_VRIKLocalVectorToModel (quakeup, lateral, forward, up, z);
	VectorNormalize (x); VectorNormalize (y); VectorNormalize (z);
	matrix[0] = x[0]; matrix[1] = y[0]; matrix[2] = z[0]; matrix[3] = origin[0];
	matrix[4] = x[1]; matrix[5] = y[1]; matrix[6] = z[1]; matrix[7] = origin[1];
	matrix[8] = x[2]; matrix[9] = y[2]; matrix[10] = z[2]; matrix[11] = origin[2];
}

static void R_VRIKRotateToward (float matrix[12], const vec3_t from,
	const vec3_t to)
{
	vec3_t a, b, axis, origin;
	float cosine, sine, one;
	float delta[12], result[12];

	VectorCopy (from, a); VectorCopy (to, b);
	if (!VectorNormalize (a) || !VectorNormalize (b))
		return;
	cosine = CLAMP(-1.0f, DotProduct (a, b), 1.0f);
	CrossProduct (a, b, axis);
	sine = VectorNormalize (axis);
	if (sine < 0.0001f)
	{
		if (cosine > 0.0f)
			return;
		axis[0] = 0, axis[1] = 0, axis[2] = 1;
		if (fabsf(DotProduct (axis, a)) > 0.9f)
			axis[0] = 0, axis[1] = 1, axis[2] = 0;
		CrossProduct (a, axis, axis);
		VectorNormalize (axis);
		sine = 0.0f;
	}
	one = 1.0f - cosine;
	delta[0] = cosine + axis[0] * axis[0] * one;
	delta[1] = axis[0] * axis[1] * one - axis[2] * sine;
	delta[2] = axis[0] * axis[2] * one + axis[1] * sine;
	delta[3] = 0;
	delta[4] = axis[1] * axis[0] * one + axis[2] * sine;
	delta[5] = cosine + axis[1] * axis[1] * one;
	delta[6] = axis[1] * axis[2] * one - axis[0] * sine;
	delta[7] = 0;
	delta[8] = axis[2] * axis[0] * one - axis[1] * sine;
	delta[9] = axis[2] * axis[1] * one + axis[0] * sine;
	delta[10] = cosine + axis[2] * axis[2] * one;
	delta[11] = 0;
	R_VRIKMatrixOrigin (matrix, origin);
	R_VRIKMatrixMultiply (delta, matrix, result);
	memcpy (matrix, result, sizeof(result));
	R_VRIKSetMatrixOrigin (matrix, origin);
}

static void R_VRIKSolveArm (const md5liveinfo_t *live, float *palette,
	qboolean rightside, vec3_t target, const vec3_t targetangles,
	vec3_t lateral, vec3_t forward, vec3_t up)
{
	int upperindex = live->jointindex[rightside ? MD5_VRIK_UPPERARM_R : MD5_VRIK_UPPERARM_L];
	int lowerindex = live->jointindex[rightside ? MD5_VRIK_LOWERARM_R : MD5_VRIK_LOWERARM_L];
	int handindex = live->jointindex[rightside ? MD5_VRIK_HAND_R : MD5_VRIK_HAND_L];
	float *upper = palette + upperindex * 12;
	float *lower = palette + lowerindex * 12;
	float *hand = palette + handindex * 12;
	float canonical[12], inverse[12], correction[12], desiredhand[12];
	vec3_t shoulder, oldelbow, oldhand, toward, pole, bendnormal, bendaxis;
	vec3_t elbow, oldupperdir, oldlowerdir, newupperdir, newlowerdir;
	float upperlength, lowerlength, distance, cosine, anglecos, anglesin;

	R_VRIKMatrixOrigin (upper, shoulder);
	R_VRIKMatrixOrigin (lower, oldelbow);
	R_VRIKMatrixOrigin (hand, oldhand);
	VectorSubtract (oldelbow, shoulder, oldupperdir);
	VectorSubtract (oldhand, oldelbow, oldlowerdir);
	upperlength = VectorLength (oldupperdir);
	lowerlength = VectorLength (oldlowerdir);
	if (upperlength < 0.01f || lowerlength < 0.01f)
		return;

	/* Preserve the MD5 hand's bone-local basis behind a canonical wrist frame. */
	R_VRIKCanonicalMatrix (oldlowerdir, up, oldhand, canonical);
	R_VRIKMatrixInverseRigid (canonical, inverse);
	R_VRIKMatrixMultiply (inverse, hand, correction);

	VectorSubtract (target, shoulder, toward);
	distance = VectorLength (toward);
	if (distance < 0.001f)
		return;
	VectorScale (toward, 1.0f / distance, toward);
	distance = CLAMP(fabsf(upperlength - lowerlength) + 0.01f, distance,
		upperlength + lowerlength - 0.01f);
	VectorScale (lateral, rightside ? 1.0f : -1.0f, pole);
	VectorMA (pole, -0.35f, forward, pole);
	CrossProduct (toward, pole, bendnormal);
	if (!VectorNormalize (bendnormal))
		VectorCopy (up, bendnormal);
	CrossProduct (bendnormal, toward, bendaxis);
	VectorNormalize (bendaxis);
	cosine = CLAMP(-1.0f,
		(upperlength * upperlength + distance * distance - lowerlength * lowerlength) /
		(2.0f * upperlength * distance), 1.0f);
	anglecos = cosine * upperlength;
	anglesin = sqrtf(q_max(0.0f, 1.0f - cosine * cosine)) * upperlength;
	VectorMA (shoulder, anglecos, toward, elbow);
	VectorMA (elbow, anglesin, bendaxis, elbow);

	VectorSubtract (elbow, shoulder, newupperdir);
	VectorSubtract (target, elbow, newlowerdir);
	R_VRIKRotateToward (upper, oldupperdir, newupperdir);
	R_VRIKRotateToward (lower, oldlowerdir, newlowerdir);
	R_VRIKSetMatrixOrigin (lower, elbow);

	R_VRIKAnglesToModelMatrix (targetangles, lateral, forward, up, target, canonical);
	R_VRIKMatrixMultiply (canonical, correction, desiredhand);
	R_VRIKSetMatrixOrigin (desiredhand, target);
	memcpy (hand, desiredhand, sizeof(desiredhand));
}

static void R_VRIKMoveJoint (const md5liveinfo_t *live, float *palette,
	md5vrikjoint_t semantic, vec3_t delta, float scale)
{
	int index = live->jointindex[semantic];
	vec3_t origin;

	if (index < 0)
		return;
	R_VRIKMatrixOrigin (palette + index * 12, origin);
	VectorMA (origin, scale, delta, origin);
	R_VRIKSetMatrixOrigin (palette + index * 12, origin);
}

static void R_VRIKSolvePalette (const md5liveinfo_t *live,
	const vrik_pose_t *pose, float *palette)
{
	vec3_t lateral, forward, up, oldhead, targethead, headdelta;
	vec3_t lefttarget, righttarget;
	float basehand[12], weaponoffset[12], inverse[12], attached[12];
	int headindex, handright, handleft, gun, axe, weapon = -1;
	qboolean dominantleft;

	if (!R_VRIKBuildBodyBasis (live, palette, lateral, forward, up))
		return;
	headindex = live->jointindex[MD5_VRIK_HEAD];
	handright = live->jointindex[MD5_VRIK_HAND_R];
	handleft = live->jointindex[MD5_VRIK_HAND_L];
	gun = live->jointindex[MD5_VRIK_GUN];
	axe = live->jointindex[MD5_VRIK_AXE];
	dominantleft = (pose->flags & VRIK_FLAG_DOMINANT_LEFT) != 0;

	/* Capture the animation's active weapon-to-right-hand offset before IK. */
	if (handright >= 0 && (gun >= 0 || axe >= 0))
	{
		vec3_t handorigin, gunorigin, axeorigin;
		float gundistance = FLT_MAX, axedistance = FLT_MAX;
		R_VRIKMatrixOrigin (palette + handright * 12, handorigin);
		if (gun >= 0)
		{
			R_VRIKMatrixOrigin (palette + gun * 12, gunorigin);
			VectorSubtract (gunorigin, handorigin, gunorigin);
			gundistance = VectorLength (gunorigin);
		}
		if (axe >= 0)
		{
			R_VRIKMatrixOrigin (palette + axe * 12, axeorigin);
			VectorSubtract (axeorigin, handorigin, axeorigin);
			axedistance = VectorLength (axeorigin);
		}
		weapon = gundistance <= axedistance ? gun : axe;
		if (q_min(gundistance, axedistance) > 64.0f)
			weapon = -1;
		if (weapon >= 0)
		{
			memcpy (basehand, palette + handright * 12, sizeof(basehand));
			R_VRIKMatrixInverseRigid (basehand, inverse);
			R_VRIKMatrixMultiply (inverse, palette + weapon * 12, weaponoffset);
		}
	}

	R_VRIKMatrixOrigin (palette + headindex * 12, oldhead);
	R_VRIKLocalVectorToModel (pose->position[VRIK_TRACKER_HEAD], lateral,
		forward, up, targethead);
	VectorSubtract (targethead, oldhead, headdelta);
	if (VectorLength (headdelta) > 24.0f)
	{
		VectorNormalize (headdelta);
		VectorScale (headdelta, 24.0f, headdelta);
		VectorAdd (oldhead, headdelta, targethead);
	}
	R_VRIKMoveJoint (live, palette, MD5_VRIK_SPINE1, headdelta, 0.12f);
	R_VRIKMoveJoint (live, palette, MD5_VRIK_SPINE2, headdelta, 0.32f);
	R_VRIKMoveJoint (live, palette, MD5_VRIK_NECK, headdelta, 0.68f);
	R_VRIKMoveJoint (live, palette, MD5_VRIK_HEAD, headdelta, 1.0f);
	R_VRIKMoveJoint (live, palette, MD5_VRIK_SHOULDER_L, headdelta, 0.32f);
	R_VRIKMoveJoint (live, palette, MD5_VRIK_SHOULDER_R, headdelta, 0.32f);
	R_VRIKMoveJoint (live, palette, MD5_VRIK_UPPERARM_L, headdelta, 0.32f);
	R_VRIKMoveJoint (live, palette, MD5_VRIK_UPPERARM_R, headdelta, 0.32f);

	/* Head rotation preserves the rig-specific bone basis like the hands. */
	{
		float body[12], target[12], correction[12], result[12];
		vec3_t bodyforward, headorigin;
		VectorCopy (forward, bodyforward);
		R_VRIKCanonicalMatrix (bodyforward, up, oldhead, body);
		R_VRIKMatrixInverseRigid (body, inverse);
		R_VRIKMatrixMultiply (inverse, palette + headindex * 12, correction);
		R_VRIKMatrixOrigin (palette + headindex * 12, headorigin);
		R_VRIKAnglesToModelMatrix (pose->orientation[VRIK_TRACKER_HEAD], lateral,
			forward, up, headorigin, target);
		R_VRIKMatrixMultiply (target, correction, result);
		R_VRIKSetMatrixOrigin (result, headorigin);
		memcpy (palette + headindex * 12, result, sizeof(result));
	}

	if (pose->flags & VRIK_FLAG_LEFT_HAND_TRACKED)
	{
		R_VRIKLocalVectorToModel (pose->position[VRIK_TRACKER_LEFT_HAND], lateral,
			forward, up, lefttarget);
		R_VRIKSolveArm (live, palette, false, lefttarget,
			pose->orientation[VRIK_TRACKER_LEFT_HAND], lateral, forward, up);
	}
	if (pose->flags & VRIK_FLAG_RIGHT_HAND_TRACKED)
	{
		R_VRIKLocalVectorToModel (pose->position[VRIK_TRACKER_RIGHT_HAND], lateral,
			forward, up, righttarget);
		R_VRIKSolveArm (live, palette, true, righttarget,
			pose->orientation[VRIK_TRACKER_RIGHT_HAND], lateral, forward, up);
	}

	/* Re-parent only the animation's in-hand prop, preserving its grip offset. */
	if (weapon >= 0)
	{
		int destination = dominantleft ? handleft : handright;
		if (destination >= 0 &&
			((dominantleft && (pose->flags & VRIK_FLAG_LEFT_HAND_TRACKED)) ||
			 (!dominantleft && (pose->flags & VRIK_FLAG_RIGHT_HAND_TRACKED))))
		{
			R_VRIKMatrixMultiply (palette + destination * 12, weaponoffset, attached);
			memcpy (palette + weapon * 12, attached, sizeof(attached));
		}
	}
}

static void R_VRIKComputeNormals (md5vertex_t *vertices, int numverts,
	const unsigned short *indexes, int numindexes)
{
	int i;

	for (i = 0; i < numverts; i++)
		vertices[i].normal[0] = vertices[i].normal[1] = vertices[i].normal[2] = 0;
	for (i = 0; i + 2 < numindexes; i += 3)
	{
		md5vertex_t *a = &vertices[indexes[i + 0]];
		md5vertex_t *b = &vertices[indexes[i + 1]];
		md5vertex_t *c = &vertices[indexes[i + 2]];
		vec3_t first, second, normal;
		VectorSubtract (b->xyz, a->xyz, first);
		VectorSubtract (c->xyz, a->xyz, second);
		CrossProduct (first, second, normal);
		VectorAdd (a->normal, normal, a->normal);
		VectorAdd (b->normal, normal, b->normal);
		VectorAdd (c->normal, normal, c->normal);
	}
	for (i = 0; i < numverts; i++)
		if (!VectorNormalize (vertices[i].normal))
			vertices[i].normal[0] = vertices[i].normal[1] = 0,
			vertices[i].normal[2] = 1;
}

static qboolean R_VRIKPrepareSkin (qmodel_t *model, const lerpdata_t *lerpdata)
{
	md5liveinfo_t live;
	md5livesurface_t surface;
	int vertex, influence;

	r_vrik_skin_active = false;
	if (!r_vrik_pose_pending || !Mod_GetMD5LiveData (model, &live) ||
		!live.compatible || !Mod_GetMD5LiveSurface (&live, 0, &surface) ||
		surface.header->nextsurface)
		return false;
	if (surface.numverts > r_vrik_skin_capacity)
	{
		md5vertex_t *resized = (md5vertex_t *)realloc (r_vrik_skin_vertices,
			(size_t)surface.numverts * sizeof(*resized));
		if (!resized)
			return false;
		r_vrik_skin_vertices = resized;
		r_vrik_skin_capacity = surface.numverts;
	}

	R_VRIKLerpPalette (&live, lerpdata, r_vrik_palette);
	R_VRIKSolvePalette (&live, &r_vrik_pending_pose, r_vrik_palette);
	for (vertex = 0; vertex < surface.numverts; vertex++)
	{
		const md5livevertex_t *source = &surface.vertices[vertex];
		md5vertex_t *destination = &r_vrik_skin_vertices[vertex];
		destination->xyz[0] = destination->xyz[1] = destination->xyz[2] = 0;
		destination->st[0] = source->st[0];
		destination->st[1] = source->st[1];
		for (influence = 0; influence < (int)source->numweights; influence++)
		{
			const md5liveweight_t *weight =
				&surface.weights[source->firstweight + influence];
			const float *matrix = r_vrik_palette + weight->joint * 12;
			vec3_t transformed;
			transformed[0] = matrix[0] * weight->position[0] +
				matrix[1] * weight->position[1] + matrix[2] * weight->position[2] +
				matrix[3] * weight->position[3];
			transformed[1] = matrix[4] * weight->position[0] +
				matrix[5] * weight->position[1] + matrix[6] * weight->position[2] +
				matrix[7] * weight->position[3];
			transformed[2] = matrix[8] * weight->position[0] +
				matrix[9] * weight->position[1] + matrix[10] * weight->position[2] +
				matrix[11] * weight->position[3];
			VectorAdd (destination->xyz, transformed, destination->xyz);
		}
	}
	R_VRIKComputeNormals (r_vrik_skin_vertices, surface.numverts,
		surface.indexes, surface.numindexes);
	r_vrik_skin_surface = surface.header;
	r_vrik_skin_active = true;
	return true;
}

static int R_VRIKReplacementFrame (const entity_t *entity,
	const qmodel_t *replacement)
{
	vec3_t movement;

	if (entity->frame >= 0 && entity->frame < replacement->numframes &&
		!q_strcasestr(entity->model->name, "ee_pl"))
		return entity->frame;
	VectorSubtract (entity->currentorigin, entity->previousorigin, movement);
	if (movement[0] * movement[0] + movement[1] * movement[1] > 0.25f)
		return 6 + ((int)(cl.time * 10.0) % 6); /* stock gun run */
	return 12 + ((int)(cl.time * 10.0) % 5); /* stock stand */
}

static qboolean R_VRIKSubstitutePlayer (entity_t *entity, entity_t *replacement)
{
	qmodel_t *model;
	vrik_pose_t pose;
	uintptr_t address;

	/* The VR option controls publication of this client's tracking. Receiving
	 * clients render any negotiated active pose automatically, including
	 * desktop spectators; non-VR players never publish one. */
	if (!cl.entities || !entity || !entity->model)
		return false;
	address = (uintptr_t)entity;
	if (address < (uintptr_t)&cl.entities[1] ||
		address > (uintptr_t)&cl.entities[cl.maxclients] ||
		entity == &cl.entities[cl.viewentity] ||
		!R_VRIKSampleEntityPose (entity, &pose))
		return false;
	model = Mod_GetRereleasePlayerMD5Model ();
	if (!model)
		return false;

	*replacement = *entity;
	replacement->model = model;
	replacement->frame = R_VRIKReplacementFrame (entity, model);
	replacement->lerpflags |= LERP_RESETANIM;
	r_vrik_pending_pose = pose;
	r_vrik_pose_pending = true;
	return true;
}

/*
==============================================================================

					MD5 DRAWING

Rerelease MD5 poses are baked to float vertices while loading.  Textured
passes interpolate those poses on the GPU when available, while diagnostic
and untextured modes retain the fixed-function path below.
==============================================================================
*/

static aliashdr_t *R_NextMD5Surface (aliashdr_t *surface)
{
	return surface->nextsurface ?
		(aliashdr_t *)((byte *)surface + surface->nextsurface) : NULL;
}

static int R_MD5SurfaceSkin (const aliashdr_t *surface, int skinnum)
{
	if (skinnum < 0 || skinnum >= surface->numskins || !surface->gltextures[skinnum][0])
		return 0;
	return skinnum;
}

static qboolean R_MD5UsesAlpha (aliashdr_t *surface, int skinnum, int anim)
{
	while (surface)
	{
		gltexture_t *texture = surface->gltextures[R_MD5SurfaceSkin (surface, skinnum)][anim];
		if (texture && (texture->flags & TEXPREF_ALPHA))
			return true;
		surface = R_NextMD5Surface (surface);
	}
	return false;
}

static void *GLMD5_GetXYZOffset (const aliashdr_t *surface, int surfaceindex, int pose)
{
	return (void *)(intptr_t)((size_t)currententity->model->md5vboxyzofs[surfaceindex] +
		(size_t)pose * surface->numverts * sizeof(md5vertex_t) + offsetof(md5vertex_t, xyz));
}

static void *GLMD5_GetNormalOffset (const aliashdr_t *surface, int surfaceindex, int pose)
{
	return (void *)(intptr_t)((size_t)currententity->model->md5vboxyzofs[surfaceindex] +
		(size_t)pose * surface->numverts * sizeof(md5vertex_t) + offsetof(md5vertex_t, normal));
}

static void GL_DrawMD5Frame_GLSL (const aliashdr_t *surface, int surfaceindex,
	lerpdata_t lerpdata, gltexture_t *texture, const vec3_t drawcolor,
	float drawalpha, qboolean drawshading, qboolean usetexture, qboolean usefog)
{
	float blend = lerpdata.pose1 != lerpdata.pose2 ? lerpdata.blend : 0.0f;
	float sscale;
	float tscale;

	GL_UseProgram (r_md5_program);
	GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->md5meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, currententity->model->md5meshindexesvbo);

	GL_EnableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_EnableVertexAttribArrayFunc (pose2NormalAttrIndex);

	GL_VertexAttribPointerFunc (texCoordsAttrIndex, 2, GL_FLOAT, GL_FALSE,
		sizeof(md5vertex_t), (void *)(intptr_t)currententity->model->md5vbostofs[surfaceindex]);
	GL_VertexAttribPointerFunc (pose1VertexAttrIndex, 3, GL_FLOAT, GL_FALSE,
		sizeof(md5vertex_t), GLMD5_GetXYZOffset(surface, surfaceindex, lerpdata.pose1));
	GL_VertexAttribPointerFunc (pose2VertexAttrIndex, 3, GL_FLOAT, GL_FALSE,
		sizeof(md5vertex_t), GLMD5_GetXYZOffset(surface, surfaceindex, lerpdata.pose2));
	GL_VertexAttribPointerFunc (pose1NormalAttrIndex, 3, GL_FLOAT, GL_FALSE,
		sizeof(md5vertex_t), GLMD5_GetNormalOffset(surface, surfaceindex, lerpdata.pose1));
	GL_VertexAttribPointerFunc (pose2NormalAttrIndex, 3, GL_FLOAT, GL_FALSE,
		sizeof(md5vertex_t), GLMD5_GetNormalOffset(surface, surfaceindex, lerpdata.pose2));

	GL_Uniform1fFunc (md5BlendLoc, blend);
	GL_Uniform3fFunc (md5ShadevectorLoc, shadevector[0], shadevector[1], shadevector[2]);
	GL_Uniform4fFunc (md5LightColorLoc, drawcolor[0], drawcolor[1], drawcolor[2], drawalpha);
	GL_Uniform1iFunc (md5TexLoc, 0);
	GL_Uniform1iFunc (md5UseTextureLoc, usetexture ? 1 : 0);
	GL_Uniform1iFunc (md5UseAlphaTestLoc,
		(usetexture && r_md5_glsl_alphatest) ? 1 : 0);
	GL_Uniform1iFunc (md5UseShadingLoc, drawshading ? 1 : 0);
	GL_Uniform1iFunc (md5UseFogLoc, usefog ? 1 : 0);
	sscale = (float)surface->skinwidth / (float)TexMgr_PadConditional (surface->skinwidth);
	tscale = (float)surface->skinheight / (float)TexMgr_PadConditional (surface->skinheight);
	GL_Uniform1fFunc (md5TexSScaleLoc, sscale);
	GL_Uniform1fFunc (md5TexTScaleLoc, tscale);

	if (usetexture)
	{
		GL_SelectTexture (GL_TEXTURE0);
		GL_Bind (texture);
	}
	if (r_perfdebug.value)
		r_perf_alias_glsl_draws++;
	glDrawElements (GL_TRIANGLES, surface->numindexes, GL_UNSIGNED_SHORT,
		(void *)(intptr_t)currententity->model->md5vboindexofs[surfaceindex]);

	GL_DisableVertexAttribArrayFunc (texCoordsAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2VertexAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose1NormalAttrIndex);
	GL_DisableVertexAttribArrayFunc (pose2NormalAttrIndex);
	GL_UseProgram (0);
	GL_SelectTexture (GL_TEXTURE0);
	rs_aliaspasses += surface->numtris;
}

static void GL_DrawMD5Frame (aliashdr_t *surface, lerpdata_t lerpdata)
{
	const md5vertex_t *verts1, *verts2;
	const unsigned short *indexes;
	float blend, iblend, sscale, tscale;
	int index, i;
	qboolean lerping;

	if (r_vrik_skin_active && surface == r_vrik_skin_surface)
	{
		verts1 = verts2 = r_vrik_skin_vertices;
		lerpdata.pose1 = lerpdata.pose2 = 0;
	}
	else
	{
		verts1 = (const md5vertex_t *)((const byte *)surface + surface->vertexes) +
			lerpdata.pose1 * surface->numverts;
		verts2 = (const md5vertex_t *)((const byte *)surface + surface->vertexes) +
			lerpdata.pose2 * surface->numverts;
	}
	indexes = (const unsigned short *)((const byte *)surface + surface->indexes);
	lerping = lerpdata.pose1 != lerpdata.pose2;
	blend = lerping ? lerpdata.blend : 0.0f;
	iblend = 1.0f - blend;
	sscale = (float)surface->skinwidth / (float)TexMgr_PadConditional(surface->skinwidth);
	tscale = (float)surface->skinheight / (float)TexMgr_PadConditional(surface->skinheight);

	glBegin (GL_TRIANGLES);
	for (i = 0; i < surface->numindexes; i++)
	{
		const md5vertex_t *v1, *v2;

		index = indexes[i];
		v1 = verts1 + index;
		v2 = verts2 + index;
		glTexCoord2f (v2->st[0] * sscale, v2->st[1] * tscale);
		if (shading)
		{
			vec3_t normal;
			float dot;

			if (lerping)
			{
				normal[0] = v1->normal[0] * iblend + v2->normal[0] * blend;
				normal[1] = v1->normal[1] * iblend + v2->normal[1] * blend;
				normal[2] = v1->normal[2] * iblend + v2->normal[2] * blend;
				VectorNormalize (normal);
			}
			else
				VectorCopy (v2->normal, normal);
			dot = DotProduct (normal, shadevector);
			if (dot < 0.0f)
				dot = 1.0f + dot * (13.0f / 44.0f);
			else
				dot = 1.0f + dot;
			glColor4f (dot * lightcolor[0], dot * lightcolor[1],
				dot * lightcolor[2], entalpha);
		}
		if (lerping)
			glVertex3f (v1->xyz[0] * iblend + v2->xyz[0] * blend,
				v1->xyz[1] * iblend + v2->xyz[1] * blend,
				v1->xyz[2] * iblend + v2->xyz[2] * blend);
		else
			glVertex3fv (v2->xyz);
	}
	glEnd ();

	rs_aliaspasses += surface->numtris;
}

static int R_MD5TriangleCount (aliashdr_t *surface)
{
	int tris = 0;

	while (surface)
	{
		tris += surface->numtris;
		surface = R_NextMD5Surface (surface);
	}
	return tris;
}

static void R_DrawMD5Pass (aliashdr_t *surface, lerpdata_t lerpdata,
	int skinnum, qboolean fullbright, const vec3_t drawcolor, float drawalpha,
	qboolean drawshading, qboolean drawfog)
{
	int anim = (int)(cl.time * 10) & 3;
	int surfaceindex = 0;

	while (surface)
	{
		int skin = R_MD5SurfaceSkin (surface, skinnum);
		gltexture_t *texture = fullbright ? surface->fbtextures[skin][anim] :
			surface->gltextures[skin][anim];

		if (texture || !fullbright)
		{
			if (r_md5_glsl_active)
				GL_DrawMD5Frame_GLSL (surface, surfaceindex, lerpdata, texture,
					drawcolor, drawalpha, drawshading, true, drawfog);
			else
			{
				GL_Bind (texture);
				GL_DrawMD5Frame (surface, lerpdata);
			}
		}
		surface = R_NextMD5Surface (surface);
		surfaceindex++;
	}
}

static void R_DrawMD5UntexturedPass (aliashdr_t *surface, lerpdata_t lerpdata,
	const vec3_t drawcolor, float drawalpha, qboolean drawfog)
{
	int surfaceindex = 0;
	qboolean useglsl = !r_vrik_skin_active && r_md5_program != 0 && currententity->model->md5meshvbo != 0 &&
		currententity->model->md5meshindexesvbo != 0;

	while (surface)
	{
		if (useglsl)
			/* See the MD3 path: the shader emits a solid color without a texture. */
			GL_DrawMD5Frame_GLSL (surface, surfaceindex, lerpdata, NULL,
				drawcolor, drawalpha, false, false, drawfog);
		else
			GL_DrawMD5Frame (surface, lerpdata);
		surface = R_NextMD5Surface (surface);
		surfaceindex++;
	}
}

static void R_DrawMD5Model (entity_t *e, qboolean cull, qboolean viewmodel)
{
	aliashdr_t *md5;
	lerpdata_t lerpdata;
	vec3_t fullbrightcolor;
	vec3_t drawflatcolor = {1, 1, 1};
	int skinnum, anim;
	qboolean alphatest = false;
	qboolean drawfog;
	float fovscale = 1.0f;

	md5 = Mod_GetMD5Extradata (e->model);
	if (!md5)
		return;

	if (r_perfdebug.value)
		r_perf_alias_draws++;
	R_SetupAliasFrame (md5, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	if (r_vrik_pose_pending)
		R_VRIKPrepareSkin (e->model, &lerpdata);
	if (cull && R_CullModelForEntity(e))
	{
		if (r_perfdebug.value)
			r_perf_alias_culled++;
		r_vrik_skin_active = false;
		return;
	}

	GL_AliasBatch_End ();
	if (!vr_enabled.value && e == &cl.viewent && scr_fov.value > 90.f && cl_gun_fovscale.value)
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));
	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (md5->scale_origin[0], md5->scale_origin[1] * fovscale,
		md5->scale_origin[2] * fovscale);
	glScalef (md5->scale[0], md5->scale[1] * fovscale, md5->scale[2] * fovscale);

	if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
		glShadeModel (GL_SMOOTH);
	if (gl_affinemodels.value)
		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	overbright = !!gl_overbright_models.value;
	shading = true;
	entalpha = (r_drawflat_cheatsafe || r_lightmap_cheatsafe) ? 1.0f : ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0.0f)
		goto cleanup;
	fullbrightcolor[0] = fullbrightcolor[1] = fullbrightcolor[2] = entalpha;
	if (entalpha < 1.0f)
	{
		overbright = false;
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
	}

	skinnum = e->skinnum;
	if (skinnum < 0 || skinnum >= md5->numskins)
		skinnum = 0;
	anim = (int)(cl.time * 10) & 3;
	alphatest = R_MD5UsesAlpha (md5, skinnum, anim);
	if (alphatest)
		glEnable (GL_ALPHA_TEST);
	rs_aliaspolys += R_MD5TriangleCount (md5);
	if (!viewmodel)
		R_SetupAliasLighting (e);
	drawfog = !viewmodel && Fog_GetDensity() > 0.0f;
	GL_DisableMultitexture ();
	/* Keep legacy composition as separate GLSL draws so overbright and
	 * fullbright overlays retain their additive fog behavior. */
	r_md5_glsl_active = !r_vrik_skin_active && !r_drawflat_cheatsafe && !r_lightmap_cheatsafe &&
		r_md5_program != 0 && e->model->md5meshvbo != 0 &&
		e->model->md5meshindexesvbo != 0;
	r_md5_glsl_alphatest = r_md5_glsl_active && alphatest;
	if (r_md5_glsl_active && alphatest)
		glDisable (GL_ALPHA_TEST); /* shader tests texture alpha multiplied by entalpha */

	if (viewmodel)
	{
		overbright = false;
		shading = false;
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		R_SetViewModelColor (entalpha);
		R_DrawMD5Pass (md5, lerpdata, skinnum, false, vr_weaponcolor,
			entalpha, false, false);
		if (gl_fullbrights.value)
		{
			glEnable (GL_BLEND);
			glBlendFunc (GL_ONE, GL_ONE);
			glDepthMask (GL_FALSE);
			glColor4f (entalpha, entalpha, entalpha, entalpha);
			R_DrawMD5Pass (md5, lerpdata, skinnum, true,
				fullbrightcolor, entalpha, false, false);
			glDepthMask (GL_TRUE);
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
		}
	}
	else if (r_drawflat_cheatsafe || r_lightmap_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		shading = false;
		glColor4f (1, 1, 1, entalpha);
		R_DrawMD5UntexturedPass (md5, lerpdata, drawflatcolor, entalpha, drawfog);
		glEnable (GL_TEXTURE_2D);
	}
	else
	{
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		if (r_fullbright_cheatsafe)
		{
			shading = false;
			glColor4f (1, 1, 1, entalpha);
			R_DrawMD5Pass (md5, lerpdata, skinnum, false,
				drawflatcolor, entalpha, false, drawfog);
		}
		else
		{
			R_DrawMD5Pass (md5, lerpdata, skinnum, false, lightcolor,
				entalpha, true, drawfog);
			/* Draw the normal pass again against additive black fog, matching the
			 * fixed-function overbright composition after fog is applied. */
			if (overbright)
			{
				glEnable (GL_BLEND);
				glBlendFunc (GL_ONE, GL_ONE);
				glDepthMask (GL_FALSE);
				Fog_StartAdditive ();
				R_DrawMD5Pass (md5, lerpdata, skinnum, false, lightcolor,
					entalpha, true, drawfog);
				Fog_StopAdditive ();
				glDepthMask (GL_TRUE);
				glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable (GL_BLEND);
			}
		}
		if (gl_fullbrights.value)
		{
			glEnable (GL_BLEND);
			glBlendFunc (GL_ONE, GL_ONE);
			glDepthMask (GL_FALSE);
			shading = false;
			glColor4f (entalpha, entalpha, entalpha, entalpha);
			Fog_StartAdditive ();
			R_DrawMD5Pass (md5, lerpdata, skinnum, true,
				fullbrightcolor, entalpha, false, drawfog);
			Fog_StopAdditive ();
			glDepthMask (GL_TRUE);
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
		}
	}

cleanup:
	r_md5_glsl_active = false;
	r_md5_glsl_alphatest = false;
	r_vrik_skin_active = false;
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glShadeModel (GL_FLAT);
	glDepthMask (GL_TRUE);
	glDisable (GL_BLEND);
	if (alphatest)
		glDisable (GL_ALPHA_TEST);
	glColor3f (1, 1, 1);
	glPopMatrix ();
}

/*
=================
R_DrawAliasModel_ShowSkel

MD5 mesh rendering expands each animation pose to skinned vertices while the
model is loaded.  r_showskel retains the evaluated 3x4 joint matrices in the
first surface header, then interpolates each joint position once before
emitting the parent links.  Keeping that O(bones) pass avoids recomputing a
parent joint for every line.
=================
*/
void R_DrawAliasModel_ShowSkel (entity_t *e)
{
	aliashdr_t *md5;
	entity_t *previousentity;
	lerpdata_t lerpdata;
	const int *parents;
	const float *pose1, *pose2;
	vec3_t positions[MAX_MD5_JOINTS];
	float fovscale = 1.0f;
	int i;

	if (!e || !e->model || !Mod_UseMD5ModelForFrame (e->model, e->skinnum, e->frame))
		return;
	if (R_CullModelForEntity (e))
		return;

	md5 = Mod_GetMD5Extradata (e->model);
	if (!md5 || md5->md5_numbones < 1 || md5->md5_numbones > MAX_MD5_JOINTS ||
		!md5->md5_boneparents || !md5->md5_boneposes)
		return;

	previousentity = currententity;
	currententity = e;
	R_SetupAliasFrame (md5, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	currententity = previousentity;
	parents = (const int *)((const byte *)md5 + md5->md5_boneparents);
	pose1 = (const float *)((const byte *)md5 + md5->md5_boneposes) +
		(size_t)lerpdata.pose1 * md5->md5_numbones * 12;
	pose2 = (const float *)((const byte *)md5 + md5->md5_boneposes) +
		(size_t)lerpdata.pose2 * md5->md5_numbones * 12;

	for (i = 0; i < md5->md5_numbones; i++)
	{
		const float *a = pose1 + i * 12;
		const float *b = pose2 + i * 12;
		positions[i][0] = a[3] + (b[3] - a[3]) * lerpdata.blend;
		positions[i][1] = a[7] + (b[7] - a[7]) * lerpdata.blend;
		positions[i][2] = a[11] + (b[11] - a[11]) * lerpdata.blend;
	}

	if (!vr_enabled.value && e == &cl.viewent && scr_fov.value > 90.f && cl_gun_fovscale.value)
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));
	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (md5->scale_origin[0], md5->scale_origin[1] * fovscale,
		md5->scale_origin[2] * fovscale);
	glScalef (md5->scale[0], md5->scale[1] * fovscale, md5->scale[2] * fovscale);
	glBegin (GL_LINES);
	for (i = 0; i < md5->md5_numbones; i++)
		if (parents[i] >= 0 && parents[i] < md5->md5_numbones)
		{
			glVertex3fv (positions[parents[i]]);
			glVertex3fv (positions[i]);
		}
	glEnd ();
	glPopMatrix ();
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel (entity_t *e)
{
	entity_t	vrikentity;
	entity_t	*savedentity;
	aliashdr_t	*paliashdr;
	int		anim, skinnum;
	gltexture_t	*tx, *fb;
	lerpdata_t	lerpdata;
	qboolean	alphatest = !!(e->model->flags & MF_HOLEY);
	float		fovscale = 1.0f;

	if (R_VRIKSubstitutePlayer (e, &vrikentity))
	{
		savedentity = currententity;
		currententity = &vrikentity;
		/* Tracked hands can leave the animation's static bounds. */
		R_DrawMD5Model (&vrikentity, false, false);
		currententity = savedentity;
		r_vrik_pose_pending = false;
		r_vrik_skin_active = false;
		return;
	}

	if (Mod_UseMD3ModelForFrame (e->model, e->skinnum, e->frame))
	{
		R_DrawMD3Model (e, true, false);
		return;
	}
	if (Mod_UseMD5ModelForFrame (e->model, e->skinnum, e->frame))
	{
		R_DrawMD5Model (e, true, false);
		return;
	}

	if (r_perfdebug.value)
		r_perf_alias_draws++;

	//
	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	//
	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	//
	// cull it
	//
	if (R_CullModelForEntity(e))
	{
		if (r_perfdebug.value)
			r_perf_alias_culled++;
		return;
	}

	//
	// transform it
	//
	if (!vr_enabled.value && e == &cl.viewent && scr_fov.value > 90.f && cl_gun_fovscale.value)
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1] * fovscale, paliashdr->scale_origin[2] * fovscale);
	glScalef (paliashdr->scale[0], paliashdr->scale[1] * fovscale, paliashdr->scale[2] * fovscale);

	//
	// random stuff
	//
	if (gl_smoothmodels.value && !r_drawflat_cheatsafe)
		glShadeModel (GL_SMOOTH);
	if (gl_affinemodels.value)
		glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
	overbright = !!gl_overbright_models.value;
	shading = true;

	//
	// set up for alpha blending
	//
	if (r_drawflat_cheatsafe || r_lightmap_cheatsafe) //no alpha in drawflat or lightmap mode
		entalpha = 1;
	else
		entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0)
		goto cleanup;
	if (entalpha < 1)
	{
		if (!gl_texture_env_combine) overbright = false; //overbright can't be done in a single pass without combiners
		glDepthMask(GL_FALSE);
		glEnable(GL_BLEND);
	}
	else if (alphatest)
		glEnable (GL_ALPHA_TEST);

	//
	// set up lighting
	//
	rs_aliaspolys += paliashdr->numtris;
	R_SetupAliasLighting (e);

	//
	// set up textures
	//
	GL_DisableMultitexture();
	anim = (int)(cl.time*10) & 3;
	skinnum = e->skinnum;
	if ((skinnum >= paliashdr->numskins) || (skinnum < 0))
	{
		Con_DPrintf ("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum, e->model->name);
		// ericw -- display skin 0 for winquake compatibility
		skinnum = 0;
	}
	tx = paliashdr->gltextures[skinnum][anim];
	fb = paliashdr->fbtextures[skinnum][anim];
	if (e->colormap != vid.colormap && !gl_nocolors.value)
	{
		if ((uintptr_t)e >= (uintptr_t)&cl.entities[1] && (uintptr_t)e <= (uintptr_t)&cl.entities[cl.maxclients]) /* && !strcmp (currententity->model->name, "progs/player.mdl") */
			tx = playertextures[e - cl.entities - 1];
	}
	if (!gl_fullbrights.value)
		fb = NULL;

	/*
	 * The opaque sorted alias scope gives repeated, state-identical models a
	 * safe ordering.  Keep every multipass, remapped, alpha-tested, viewmodel,
	 * and cheat/debug path on the established renderer.
	 */
	if (GL_AliasInstanced_Available () &&
		e != &cl.viewent && entalpha == 1.0f && !alphatest &&
		!r_drawflat_cheatsafe && !r_fullbright_cheatsafe && !r_lightmap_cheatsafe &&
		fb == NULL && tx != NULL && e->colormap == vid.colormap &&
		e->model->meshvbo != 0 && e->model->meshindexesvbo != 0 &&
		GL_AliasInstanced_EnsureBuffer ())
	{
		glPopMatrix ();
		GL_AliasInstanced_Queue (e, paliashdr, &lerpdata, tx);
		return;
	}

	/* A legacy draw must appear after any queued instance group.  It reached
	 * this point with its own fixed-function model transform on the stack, so
	 * briefly restore the view matrix before flushing the prior group. */
	glPopMatrix ();
	GL_AliasInstanced_Flush ();
	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1] * fovscale, paliashdr->scale_origin[2] * fovscale);
	glScalef (paliashdr->scale[0], paliashdr->scale[1] * fovscale, paliashdr->scale[2] * fovscale);

	//
	// draw it
	//
	if (r_drawflat_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		GL_DrawAliasFrame (paliashdr, lerpdata);
		glEnable (GL_TEXTURE_2D);
		srand((int) (cl.time * 1000)); //restore randomness
	}
	else if (r_fullbright_cheatsafe)
	{
		GL_Bind (tx);
		shading = false;
		glColor4f(1,1,1,entalpha);
		GL_DrawAliasFrame (paliashdr, lerpdata);
		if (fb)
		{
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_Bind(fb);
			glEnable(GL_BLEND);
			glBlendFunc (GL_ONE, GL_ONE);
			glDepthMask(GL_FALSE);
			glColor3f(entalpha,entalpha,entalpha);
			Fog_StartAdditive ();
			GL_DrawAliasFrame (paliashdr, lerpdata);
			Fog_StopAdditive ();
			glDepthMask(GL_TRUE);
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_BLEND);
		}
	}
	else if (r_lightmap_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		shading = false;
		glColor3f(1,1,1);
		GL_DrawAliasFrame (paliashdr, lerpdata);
		glEnable (GL_TEXTURE_2D);
	}
// call fast path if possible. if the shader compliation failed for some reason,
// r_alias_program will be 0.
	else if (r_alias_program != 0)
	{
		GL_DrawAliasFrame_GLSL (paliashdr, lerpdata, tx, fb);
	}
	else if (overbright)
	{
		if  (gl_texture_env_combine && gl_mtexable && gl_texture_env_add && fb) //case 1: everything in one pass
		{
			GL_Bind (tx);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			GL_EnableMultitexture(); // selects TEXTURE1
			GL_Bind (fb);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			glEnable(GL_BLEND);
			GL_DrawAliasFrame (paliashdr, lerpdata);
			glDisable(GL_BLEND);
			GL_DisableMultitexture();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else if (gl_texture_env_combine) //case 2: overbright in one pass, then fullbright pass
		{
		// first pass
			GL_Bind(tx);
			glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE_EXT);
			glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_EXT, GL_MODULATE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_EXT, GL_TEXTURE);
			glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE1_RGB_EXT, GL_PRIMARY_COLOR_EXT);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 2.0f);
			GL_DrawAliasFrame (paliashdr, lerpdata);
			glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE_EXT, 1.0f);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		// second pass
			if (fb)
			{
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_Bind(fb);
				glEnable(GL_BLEND);
				glBlendFunc (GL_ONE, GL_ONE);
				glDepthMask(GL_FALSE);
				shading = false;
				glColor3f(entalpha,entalpha,entalpha);
				Fog_StartAdditive ();
				GL_DrawAliasFrame (paliashdr, lerpdata);
				Fog_StopAdditive ();
				glDepthMask(GL_TRUE);
				glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable(GL_BLEND);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
		}
		else //case 3: overbright in two passes, then fullbright pass
		{
		// first pass
			GL_Bind(tx);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DrawAliasFrame (paliashdr, lerpdata);
		// second pass -- additive with black fog, to double the object colors but not the fog color
			glEnable(GL_BLEND);
			glBlendFunc (GL_ONE, GL_ONE);
			glDepthMask(GL_FALSE);
			Fog_StartAdditive ();
			GL_DrawAliasFrame (paliashdr, lerpdata);
			Fog_StopAdditive ();
			glDepthMask(GL_TRUE);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_BLEND);
		// third pass
			if (fb)
			{
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
				GL_Bind(fb);
				glEnable(GL_BLEND);
				glBlendFunc (GL_ONE, GL_ONE);
				glDepthMask(GL_FALSE);
				shading = false;
				glColor3f(entalpha,entalpha,entalpha);
				Fog_StartAdditive ();
				GL_DrawAliasFrame (paliashdr, lerpdata);
				Fog_StopAdditive ();
				glDepthMask(GL_TRUE);
				glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable(GL_BLEND);
				glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
		}
	}
	else
	{
		if (gl_mtexable && gl_texture_env_add && fb) //case 4: fullbright mask using multitexture
		{
			GL_DisableMultitexture(); // selects TEXTURE0
			GL_Bind (tx);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_EnableMultitexture(); // selects TEXTURE1
			GL_Bind (fb);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
			glEnable(GL_BLEND);
			GL_DrawAliasFrame (paliashdr, lerpdata);
			glDisable(GL_BLEND);
			GL_DisableMultitexture();
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		}
		else //case 5: fullbright mask without multitexture
		{
		// first pass
			GL_Bind(tx);
			glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
			GL_DrawAliasFrame (paliashdr, lerpdata);
		// second pass
			if (fb)
			{
				GL_Bind(fb);
				glEnable(GL_BLEND);
				glBlendFunc (GL_ONE, GL_ONE);
				glDepthMask(GL_FALSE);
				shading = false;
				glColor3f(entalpha,entalpha,entalpha);
				Fog_StartAdditive ();
				GL_DrawAliasFrame (paliashdr, lerpdata);
				Fog_StopAdditive ();
				glDepthMask(GL_TRUE);
				glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				glDisable(GL_BLEND);
			}
		}
	}

cleanup:
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glShadeModel (GL_FLAT);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	if (alphatest)
		glDisable (GL_ALPHA_TEST);
	glColor3f(1,1,1);
	glPopMatrix ();
}

static void R_DrawMD3ModelOutline (entity_t *e, float r, float g, float b,
	float a, float inflate)
{
	aliashdr_t *md3;
	lerpdata_t lerpdata;
	vec3_t color = {r, g, b};

	md3 = Mod_GetMD3Extradata (e->model);
	if (!md3)
		return;
	R_SetupAliasFrame (md3, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (md3->scale_origin[0], md3->scale_origin[1], md3->scale_origin[2]);
	glScalef (md3->scale[0], md3->scale[1], md3->scale[2]);
	if (inflate != 1.0f)
		glScalef (inflate, inflate, inflate);
	GL_AliasBatch_End ();
	GL_DisableMultitexture ();
	glDisable (GL_TEXTURE_2D);
	shading = false;
	entalpha = a;
	glColor4f (r, g, b, a);
	R_DrawMD3UntexturedPass (md3, lerpdata, color, a, false);
	glEnable (GL_TEXTURE_2D);
	glPopMatrix ();
}

static void R_DrawMD5ModelOutline (entity_t *e, float r, float g, float b,
	float a, float inflate)
{
	aliashdr_t *md5;
	lerpdata_t lerpdata;
	vec3_t color = {r, g, b};

	md5 = Mod_GetMD5Extradata (e->model);
	if (!md5)
		return;
	R_SetupAliasFrame (md5, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (md5->scale_origin[0], md5->scale_origin[1], md5->scale_origin[2]);
	glScalef (md5->scale[0], md5->scale[1], md5->scale[2]);
	if (inflate != 1.0f)
		glScalef (inflate, inflate, inflate);
	GL_AliasBatch_End ();
	GL_DisableMultitexture ();
	glDisable (GL_TEXTURE_2D);
	shading = false;
	entalpha = a;
	glColor4f (r, g, b, a);
	R_DrawMD5UntexturedPass (md5, lerpdata, color, a, false);
	glEnable (GL_TEXTURE_2D);
	glPopMatrix ();
}

/*
=================
R_DrawAliasModelOutline -- Draw a flat-colored alias model mesh.
Used for player outline rendering (visible through walls in co-op VR).
No frustum culling, no texturing, no lighting -- just a solid color mesh.
=================
*/
void R_DrawAliasModelOutline (entity_t *e, float r, float g, float b, float a, float inflate)
{
	aliashdr_t	*paliashdr;
	lerpdata_t	lerpdata;

	if (Mod_UseMD3ModelForFrame (e->model, e->skinnum, e->frame))
	{
		R_DrawMD3ModelOutline (e, r, g, b, a, inflate);
		return;
	}
	if (Mod_UseMD5ModelForFrame (e->model, e->skinnum, e->frame))
	{
		R_DrawMD5ModelOutline (e, r, g, b, a, inflate);
		return;
	}

	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

	// Scale around approximate model center for outline inflation
	if (inflate != 1.0f)
	{
		glTranslatef (127.5f, 127.5f, 127.5f);
		glScalef (inflate, inflate, inflate);
		glTranslatef (-127.5f, -127.5f, -127.5f);
	}

	GL_DisableMultitexture ();
	glDisable (GL_TEXTURE_2D);
	shading = false;
	entalpha = a;
	glColor4f (r, g, b, a);
	GL_DrawAliasFrame (paliashdr, lerpdata);
	glEnable (GL_TEXTURE_2D);

	glPopMatrix ();
}

/*
=================
R_DrawAliasModel_NoCull -- VR weapon wheel: identical to R_DrawAliasModel
but WITHOUT R_CullModelForEntity frustum check, so UI elements always render.
=================
*/
void R_DrawAliasModel_NoCull (entity_t *e)
{
	aliashdr_t	*paliashdr;
	int	anim, skinnum;
	gltexture_t	*tx, *fb;
	lerpdata_t	lerpdata;

	if (!e || !e->model)
		return;
	if (Mod_UseMD3ModelForFrame (e->model, e->skinnum, e->frame))
	{
		R_DrawMD3Model (e, false, true);
		return;
	}
	if (Mod_UseMD5ModelForFrame (e->model, e->skinnum, e->frame))
	{
		R_DrawMD5Model (e, false, true);
		return;
	}

	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	if (!paliashdr)
		return;

	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

	overbright = false;
	shading = false;

	entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0)
		goto cleanup_nocull;

	rs_aliaspolys += paliashdr->numtris;

	GL_DisableMultitexture();
	anim = (int)(cl.time*10) & 3;
	skinnum = e->skinnum;
	if ((skinnum >= paliashdr->numskins) || (skinnum < 0))
		skinnum = 0;
	tx = paliashdr->gltextures[skinnum][anim];
	fb = paliashdr->fbtextures[skinnum][anim];

	GL_Bind(tx);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	
	extern vec3_t vr_weaponcolor;
	if (vr_weaponcolor[0] != 1.0f || vr_weaponcolor[1] != 1.0f || vr_weaponcolor[2] != 1.0f) {
		glColor4f(vr_weaponcolor[0], vr_weaponcolor[1], vr_weaponcolor[2], entalpha);
	} else {
		glColor4f(1, 1, 1, entalpha);
	}
	GL_DrawAliasFrame (paliashdr, lerpdata);

	if (fb)
	{
		GL_Bind(fb);
		glEnable(GL_BLEND);
		glBlendFunc (GL_ONE, GL_ONE);
		glDepthMask(GL_FALSE);
		glColor3f(entalpha, entalpha, entalpha);
		GL_DrawAliasFrame (paliashdr, lerpdata);
		glDepthMask(GL_TRUE);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
	}

cleanup_nocull:
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glShadeModel (GL_FLAT);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glColor3f(1,1,1);
	glPopMatrix ();
}

//johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.7 //skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y 0 //skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0 //0=completely flat
#define SHADOW_HEIGHT 0.1 //how far above the floor to render the shadow
//johnfitz

static void GL_DrawMD3Shadow (entity_t *e)
{
	float shadowmatrix[16] = {1, 0, 0, 0,
		0, 1, 0, 0,
		SHADOW_SKEW_X, SHADOW_SKEW_Y, SHADOW_VSCALE, 0,
		0, 0, SHADOW_HEIGHT, 1};
	float lheight;
	aliashdr_t *md3;
	lerpdata_t lerpdata;
	vec3_t color = {0, 0, 0};

	md3 = Mod_GetMD3Extradata (e->model);
	if (!md3)
		return;
	R_SetupAliasFrame (md3, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	R_LightPoint (e->origin);
	lheight = currententity->origin[2] - lightspot[2];

	glPushMatrix ();
	glTranslatef (lerpdata.origin[0], lerpdata.origin[1], lerpdata.origin[2]);
	glTranslatef (0, 0, -lheight);
	glMultMatrixf (shadowmatrix);
	glTranslatef (0, 0, lheight);
	glRotatef (lerpdata.angles[1], 0, 0, 1);
	glRotatef (-lerpdata.angles[0], 0, 1, 0);
	glRotatef (lerpdata.angles[2], 1, 0, 0);
	glTranslatef (md3->scale_origin[0], md3->scale_origin[1], md3->scale_origin[2]);
	glScalef (md3->scale[0], md3->scale[1], md3->scale[2]);
	GL_AliasBatch_End ();
	glDepthMask (GL_FALSE);
	glEnable (GL_BLEND);
	GL_DisableMultitexture ();
	glDisable (GL_TEXTURE_2D);
	shading = false;
	glColor4f (0, 0, 0, entalpha * 0.5f);
	R_DrawMD3UntexturedPass (md3, lerpdata, color, entalpha * 0.5f,
		Fog_GetDensity() > 0.0f);
	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
	glDepthMask (GL_TRUE);
	glPopMatrix ();
}

static void GL_DrawMD5Shadow (entity_t *e)
{
	float shadowmatrix[16] = {1, 0, 0, 0,
		0, 1, 0, 0,
		SHADOW_SKEW_X, SHADOW_SKEW_Y, SHADOW_VSCALE, 0,
		0, 0, SHADOW_HEIGHT, 1};
	float lheight;
	aliashdr_t *md5;
	lerpdata_t lerpdata;
	vec3_t color = {0, 0, 0};

	md5 = Mod_GetMD5Extradata (e->model);
	if (!md5)
		return;
	R_SetupAliasFrame (md5, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	R_LightPoint (e->origin);
	lheight = currententity->origin[2] - lightspot[2];
	glPushMatrix ();
	glTranslatef (lerpdata.origin[0], lerpdata.origin[1], lerpdata.origin[2]);
	glTranslatef (0, 0, -lheight);
	glMultMatrixf (shadowmatrix);
	glTranslatef (0, 0, lheight);
	glRotatef (lerpdata.angles[1], 0, 0, 1);
	glRotatef (-lerpdata.angles[0], 0, 1, 0);
	glRotatef (lerpdata.angles[2], 1, 0, 0);
	glTranslatef (md5->scale_origin[0], md5->scale_origin[1], md5->scale_origin[2]);
	glScalef (md5->scale[0], md5->scale[1], md5->scale[2]);
	GL_AliasBatch_End ();
	glDepthMask (GL_FALSE);
	glEnable (GL_BLEND);
	GL_DisableMultitexture ();
	glDisable (GL_TEXTURE_2D);
	shading = false;
	glColor4f (0, 0, 0, entalpha * 0.5f);
	R_DrawMD5UntexturedPass (md5, lerpdata, color, entalpha * 0.5f,
		Fog_GetDensity() > 0.0f);
	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
	glDepthMask (GL_TRUE);
	glPopMatrix ();
}

/*
=============
GL_DrawAliasShadow -- johnfitz -- rewritten

TODO: orient shadow onto "lightplane" (a global mplane_t*)
=============
*/
void GL_DrawAliasShadow (entity_t *e)
{
	float	shadowmatrix[16] = {1,				0,				0,				0,
								0,				1,				0,				0,
								SHADOW_SKEW_X,	SHADOW_SKEW_Y,	SHADOW_VSCALE,	0,
								0,				0,				SHADOW_HEIGHT,	1};
	float		lheight;
	aliashdr_t	*paliashdr;
	lerpdata_t	lerpdata;

	if (R_CullModelForEntity(e))
		return;

	if (e == &cl.viewent || e->model->flags & MOD_NOSHADOW)
		return;

	entalpha = ENTALPHA_DECODE(e->alpha);
	if (entalpha == 0) return;
	if (Mod_UseMD3ModelForFrame (e->model, e->skinnum, e->frame))
	{
		GL_DrawMD3Shadow (e);
		return;
	}
	if (Mod_UseMD5ModelForFrame (e->model, e->skinnum, e->frame))
	{
		GL_DrawMD5Shadow (e);
		return;
	}

	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	R_LightPoint (e->origin);
	lheight = currententity->origin[2] - lightspot[2];

// set up matrix
	glPushMatrix ();
	glTranslatef (lerpdata.origin[0],  lerpdata.origin[1],  lerpdata.origin[2]);
	glTranslatef (0,0,-lheight);
	glMultMatrixf (shadowmatrix);
	glTranslatef (0,0,lheight);
	glRotatef (lerpdata.angles[1],  0, 0, 1);
	glRotatef (-lerpdata.angles[0],  0, 1, 0);
	glRotatef (lerpdata.angles[2],  1, 0, 0);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

// draw it
	glDepthMask(GL_FALSE);
	glEnable (GL_BLEND);
	GL_DisableMultitexture ();
	glDisable (GL_TEXTURE_2D);
	shading = false;
	glColor4f(0,0,0,entalpha * 0.5);
	GL_DrawAliasFrame (paliashdr, lerpdata);
	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
	glDepthMask(GL_TRUE);

//clean up
	glPopMatrix ();
}

static void R_DrawMD3Model_ShowTris (entity_t *e)
{
	aliashdr_t *md3;
	lerpdata_t lerpdata;
	float fovscale = 1.0f;
	vec3_t color = {1, 1, 1};

	if (R_CullModelForEntity(e))
		return;
	md3 = Mod_GetMD3Extradata (e->model);
	if (!md3)
		return;
	R_SetupAliasFrame (md3, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	if (!vr_enabled.value && e == &cl.viewent && scr_fov.value > 90.f && cl_gun_fovscale.value)
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (md3->scale_origin[0], md3->scale_origin[1] * fovscale,
		md3->scale_origin[2] * fovscale);
	glScalef (md3->scale[0], md3->scale[1] * fovscale, md3->scale[2] * fovscale);
	GL_AliasBatch_End ();
	shading = false;
	glColor3f (1, 1, 1);
	R_DrawMD3UntexturedPass (md3, lerpdata, color, 1.0f, false);
	glPopMatrix ();
}

static void R_DrawMD5Model_ShowTris (entity_t *e)
{
	aliashdr_t *md5;
	lerpdata_t lerpdata;
	float fovscale = 1.0f;
	vec3_t color = {1, 1, 1};

	if (R_CullModelForEntity(e))
		return;
	md5 = Mod_GetMD5Extradata (e->model);
	if (!md5)
		return;
	R_SetupAliasFrame (md5, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);
	if (!vr_enabled.value && e == &cl.viewent && scr_fov.value > 90.f && cl_gun_fovscale.value)
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));
	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin, lerpdata.angles, e->scale);
	glTranslatef (md5->scale_origin[0], md5->scale_origin[1] * fovscale,
		md5->scale_origin[2] * fovscale);
	glScalef (md5->scale[0], md5->scale[1] * fovscale, md5->scale[2] * fovscale);
	GL_AliasBatch_End ();
	shading = false;
	glColor3f (1, 1, 1);
	R_DrawMD5UntexturedPass (md5, lerpdata, color, 1.0f, false);
	glPopMatrix ();
}

/*
=================
R_DrawAliasModel_ShowTris -- johnfitz
=================
*/
void R_DrawAliasModel_ShowTris (entity_t *e)
{
	aliashdr_t	*paliashdr;
	lerpdata_t	lerpdata;
	float	fovscale = 1.0f;

	if (Mod_UseMD3ModelForFrame (e->model, e->skinnum, e->frame))
	{
		R_DrawMD3Model_ShowTris (e);
		return;
	}
	if (Mod_UseMD5ModelForFrame (e->model, e->skinnum, e->frame))
	{
		R_DrawMD5Model_ShowTris (e);
		return;
	}

	if (R_CullModelForEntity(e))
		return;

	paliashdr = (aliashdr_t *)Mod_Extradata (e->model);
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	if (!vr_enabled.value && e == &cl.viewent && scr_fov.value > 90.f && cl_gun_fovscale.value)
		fovscale = tan(scr_fov.value * (0.5f * M_PI / 180.f));

	glPushMatrix ();
	R_RotateForEntity (lerpdata.origin,lerpdata.angles, e->scale);
	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1] * fovscale, paliashdr->scale_origin[2] * fovscale);
	glScalef (paliashdr->scale[0], paliashdr->scale[1] * fovscale, paliashdr->scale[2] * fovscale);

	shading = false;
	glColor3f(1,1,1);
	GL_DrawAliasFrame (paliashdr, lerpdata);

	glPopMatrix ();
}
