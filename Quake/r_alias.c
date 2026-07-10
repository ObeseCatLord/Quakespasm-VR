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

static GLuint r_alias_program;

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

#define pose1VertexAttrIndex 0
#define pose1NormalAttrIndex 1
#define pose2VertexAttrIndex 2
#define pose2NormalAttrIndex 3
#define texCoordsAttrIndex 4

/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static void *GLARB_GetXYZOffset (aliashdr_t *hdr, int pose)
{
	const int xyzoffs = offsetof (meshxyz_t, xyz);
	return (void *) (currententity->model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + xyzoffs);
}

/*
=============
GLARB_GetNormalOffset

Returns the offset of the first vertex's meshxyz_t.normal in the vbo for the
given model and pose.
=============
*/
static void *GLARB_GetNormalOffset (aliashdr_t *hdr, int pose)
{
	const int normaloffs = offsetof (meshxyz_t, normal);
	return (void *)(currententity->model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + normaloffs);
}

static void GL_AliasBatch_End (void)
{
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

MD3s deliberately use the existing fixed-function alias path. They retain
the engine's entity/frame lerp state, fog, alpha, fullbright, and projected
shadow behavior without requiring a second renderer or a different GL context.
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

static void R_MD3Normal (const md3vertex_t *vert, vec3_t normal)
{
	float lat = (float)vert->latlong[0] * (2.0f * (float)M_PI / 255.0f);
	float lng = (float)vert->latlong[1] * (2.0f * (float)M_PI / 255.0f);

	normal[0] = cosf (lng) * sinf (lat);
	normal[1] = sinf (lng) * sinf (lat);
	normal[2] = cosf (lat);
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
	int skinnum, qboolean fullbright)
{
	while (surface)
	{
		int skin = R_MD3SurfaceSkin (surface, skinnum);
		gltexture_t *texture = fullbright ? surface->fbtextures[skin][0] :
			surface->gltextures[skin][0];

		if (texture || !fullbright)
		{
			GL_Bind (texture);
			GL_DrawMD3Frame (surface, lerpdata);
		}
		surface = R_NextMD3Surface (surface);
	}
}

static void R_DrawMD3UntexturedPass (aliashdr_t *surface, lerpdata_t lerpdata)
{
	while (surface)
	{
		GL_DrawMD3Frame (surface, lerpdata);
		surface = R_NextMD3Surface (surface);
	}
}

static void R_DrawMD3Model (entity_t *e, qboolean cull)
{
	aliashdr_t *md3;
	lerpdata_t lerpdata;
	int skinnum;
	qboolean alphatest = false;
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
	if (entalpha < 1.0f)
	{
		overbright = false;
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
	}

	skinnum = e->skinnum;
	if (skinnum < 0 || skinnum >= md3->numskins)
		skinnum = 0;
	alphatest = !!md3->gltextures[R_MD3SurfaceSkin(md3, skinnum)][0];
	if (alphatest)
		glEnable (GL_ALPHA_TEST);

	rs_aliaspolys += R_MD3TriangleCount (md3);
	R_SetupAliasLighting (e);
	GL_DisableMultitexture ();

	if (r_drawflat_cheatsafe || r_lightmap_cheatsafe)
	{
		glDisable (GL_TEXTURE_2D);
		shading = false;
		glColor4f (1, 1, 1, entalpha);
		R_DrawMD3UntexturedPass (md3, lerpdata);
		glEnable (GL_TEXTURE_2D);
	}
	else
	{
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		if (r_fullbright_cheatsafe)
		{
			shading = false;
			glColor4f (1, 1, 1, entalpha);
			R_DrawMD3Pass (md3, lerpdata, skinnum, false);
		}
		else
		{
			R_DrawMD3Pass (md3, lerpdata, skinnum, false);
			if (overbright)
			{
				glEnable (GL_BLEND);
				glBlendFunc (GL_ONE, GL_ONE);
				glDepthMask (GL_FALSE);
				Fog_StartAdditive ();
				R_DrawMD3Pass (md3, lerpdata, skinnum, false);
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
			R_DrawMD3Pass (md3, lerpdata, skinnum, true);
			Fog_StopAdditive ();
			glDepthMask (GL_TRUE);
			glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable (GL_BLEND);
		}
	}

cleanup:
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
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel (entity_t *e)
{
	aliashdr_t	*paliashdr;
	int		anim, skinnum;
	gltexture_t	*tx, *fb;
	lerpdata_t	lerpdata;
	qboolean	alphatest = !!(e->model->flags & MF_HOLEY);
	float		fovscale = 1.0f;

	if (Mod_UseMD3Model (e->model, e->skinnum))
	{
		R_DrawMD3Model (e, true);
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
	R_DrawMD3UntexturedPass (md3, lerpdata);
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

	if (Mod_UseMD3Model (e->model, e->skinnum))
	{
		R_DrawMD3ModelOutline (e, r, g, b, a, inflate);
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
	if (Mod_UseMD3Model (e->model, e->skinnum))
	{
		R_DrawMD3Model (e, false);
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
	R_DrawMD3UntexturedPass (md3, lerpdata);
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
	if (Mod_UseMD3Model (e->model, e->skinnum))
	{
		GL_DrawMD3Shadow (e);
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
	R_DrawMD3UntexturedPass (md3, lerpdata);
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

	if (Mod_UseMD3Model (e->model, e->skinnum))
	{
		R_DrawMD3Model_ShowTris (e);
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
