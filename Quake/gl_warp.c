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
//gl_warp.c -- warping animation support

#include "quakedef.h"

extern cvar_t r_drawflat;
extern cvar_t vr_enabled;

cvar_t r_oldwater = {"r_oldwater", "0", CVAR_ARCHIVE};
cvar_t r_oldwater_max_drawpolys = {"r_oldwater_max_drawpolys", "2048", CVAR_ARCHIVE};
cvar_t r_waterquality = {"r_waterquality", "8", CVAR_NONE};
cvar_t r_waterwarp = {"r_waterwarp", "1", CVAR_NONE};

int gl_warpimagesize;
float load_subdivide_size; //johnfitz -- remember what subdivide_size value was when this map was loaded

static const float	turbsin[] = {
#include "gl_warp_sin.h"
};

#define WARPCALC(s,t) ((s + turbsin[(int)((t*2)+(cl.time*(128.0/M_PI))) & 255]) * (1.0/64)) //johnfitz -- correct warp
#define WARPCALC2(s,t) ((s + turbsin[(int)((t*0.125+cl.time)*(128.0/M_PI)) & 255]) * (1.0/64)) //johnfitz -- old warp

//==============================================================================
//
//  OLD-STYLE WATER
//
//==============================================================================

static msurface_t	*warpface;

cvar_t gl_subdivide_size = {"gl_subdivide_size", "128", CVAR_ARCHIVE};

static void BoundPoly (int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
	int		i, j;
	float	*v;

	mins[0] = mins[1] = mins[2] = FLT_MAX;
	maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
	v = verts;
	for (i=0 ; i<numverts ; i++)
		for (j=0 ; j<3 ; j++, v++)
		{
			if (*v < mins[j])
				mins[j] = *v;
			if (*v > maxs[j])
				maxs[j] = *v;
		}
}

static void SubdividePolygon (int numverts, float *verts)
{
	int		i, j, k;
	vec3_t	mins, maxs;
	float	m;
	float	*v;
	vec3_t	front[64], back[64];
	int		f, b;
	float	dist[64];
	float	frac;
	glpoly_t	*poly;
	float	s, t;

	if (numverts > 60)
		Sys_Error ("SubdividePolygon: numverts = %i", numverts);

	BoundPoly (numverts, verts, mins, maxs);

	for (i=0 ; i<3 ; i++)
	{
		m = (mins[i] + maxs[i]) * 0.5;
		m = gl_subdivide_size.value * floor (m/gl_subdivide_size.value + 0.5);
		if (maxs[i] - m < 8)
			continue;
		if (m - mins[i] < 8)
			continue;

		// cut it
		v = verts + i;
		for (j=0 ; j<numverts ; j++, v+= 3)
			dist[j] = *v - m;

		// wrap cases
		dist[j] = dist[0];
		v-=i;
		VectorCopy (verts, v);

		f = b = 0;
		v = verts;
		for (j=0 ; j<numverts ; j++, v+= 3)
		{
			if (dist[j] >= 0)
			{
				VectorCopy (v, front[f]);
				f++;
			}
			if (dist[j] <= 0)
			{
				VectorCopy (v, back[b]);
				b++;
			}
			if (dist[j] == 0 || dist[j+1] == 0)
				continue;
			if ( (dist[j] > 0) != (dist[j+1] > 0) )
			{
				// clip point
				frac = dist[j] / (dist[j] - dist[j+1]);
				for (k=0 ; k<3 ; k++)
					front[f][k] = back[b][k] = v[k] + frac*(v[3+k] - v[k]);
				f++;
				b++;
			}
		}

		SubdividePolygon (f, front[0]);
		SubdividePolygon (b, back[0]);
		return;
	}

	poly = (glpoly_t *) Hunk_Alloc (sizeof(glpoly_t) + (numverts-4) * VERTEXSIZE*sizeof(float));
	poly->next = warpface->polys->next;
	warpface->polys->next = poly;
	poly->numverts = numverts;
	for (i=0 ; i<numverts ; i++, verts+= 3)
	{
		VectorCopy (verts, poly->verts[i]);
		s = DotProduct (verts, warpface->texinfo->vecs[0]);
		t = DotProduct (verts, warpface->texinfo->vecs[1]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;
	}
}

/*
================
GL_SubdivideSurface
================
*/
void GL_SubdivideSurface (msurface_t *fa)
{
	vec3_t	verts[64];
	int		i;

	if (fa->polys->numverts > 64)
		Sys_Error ("GL_SubdivideSurface: numverts = %i", fa->polys->numverts);

	warpface = fa;

	//the first poly in the chain is the undivided poly for newwater rendering.
	//grab the verts from that.
	for (i=0; i<fa->polys->numverts; i++)
		VectorCopy (fa->polys->verts[i], verts[i]);

	SubdividePolygon (fa->polys->numverts, verts[0]);
}

/*
================
DrawWaterPoly -- johnfitz
================
*/
void DrawWaterPoly (glpoly_t *p)
{
	float	*v;
	int		i;

	if (load_subdivide_size > 48)
	{
		glBegin (GL_POLYGON);
		v = p->verts[0];
		for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
		{
			glTexCoord2f (WARPCALC2(v[3],v[4]), WARPCALC2(v[4],v[3]));
			glVertex3fv (v);
		}
		glEnd ();
	}
	else
	{
		glBegin (GL_POLYGON);
		v = p->verts[0];
		for (i=0 ; i<p->numverts ; i++, v+= VERTEXSIZE)
		{
			glTexCoord2f (WARPCALC(v[3],v[4]), WARPCALC(v[4],v[3]));
			glVertex3fv (v);
		}
		glEnd ();
	}
}

//==============================================================================
//
//  RENDER-TO-FRAMEBUFFER WATER
//
//==============================================================================

/*
 * The classic warp path draws into a corner of the current framebuffer and
 * copies those pixels into the warp texture.  That is invalid when the current
 * framebuffer is multisampled, relies on the framebuffer being window-sized,
 * and cannot work while GL_SetCanvas intentionally leaves a VR eye projection
 * alone.  Render directly into each single-sample warp texture whenever FBOs
 * are available.  Keep the copy path only for non-VR, single-sample contexts
 * which predate framebuffer objects.
 */
static PFNGLBINDFRAMEBUFFEREXTPROC warp_BindFramebuffer;
static PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC warp_CheckFramebufferStatus;
static PFNGLGENFRAMEBUFFERSEXTPROC warp_GenFramebuffers;
static PFNGLFRAMEBUFFERTEXTURE2DEXTPROC warp_FramebufferTexture2D;
static PFNGLISFRAMEBUFFEREXTPROC warp_IsFramebuffer;
static GLuint warp_framebuffer;
static qboolean warp_fbo_functions_checked;
static qboolean warp_fbo_warning_printed;
#if defined(USE_SDL2)
static SDL_GLContext warp_gl_context;
#endif

static void *Warp_GetFramebufferProc (const char *core_name, const char *ext_name)
{
	void *proc = SDL_GL_GetProcAddress (core_name);
	if (!proc)
		proc = SDL_GL_GetProcAddress (ext_name);
	return proc;
}

static qboolean Warp_InitFramebuffer (void)
{
#if defined(USE_SDL2)
	SDL_GLContext current_context = SDL_GL_GetCurrentContext ();

	/* Function pointers and object names belong to the current GL context. */
	if (current_context != warp_gl_context)
	{
		warp_gl_context = current_context;
		warp_framebuffer = 0;
		warp_fbo_functions_checked = false;
	}
#endif

	if (!warp_fbo_functions_checked)
	{
		warp_fbo_functions_checked = true;
		warp_BindFramebuffer = (PFNGLBINDFRAMEBUFFEREXTPROC)
			Warp_GetFramebufferProc ("glBindFramebuffer", "glBindFramebufferEXT");
		warp_CheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
			Warp_GetFramebufferProc ("glCheckFramebufferStatus", "glCheckFramebufferStatusEXT");
		warp_GenFramebuffers = (PFNGLGENFRAMEBUFFERSEXTPROC)
			Warp_GetFramebufferProc ("glGenFramebuffers", "glGenFramebuffersEXT");
		warp_FramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
			Warp_GetFramebufferProc ("glFramebufferTexture2D", "glFramebufferTexture2DEXT");
		warp_IsFramebuffer = (PFNGLISFRAMEBUFFEREXTPROC)
			Warp_GetFramebufferProc ("glIsFramebuffer", "glIsFramebufferEXT");
	}

	if (!warp_BindFramebuffer || !warp_CheckFramebufferStatus ||
		!warp_GenFramebuffers || !warp_FramebufferTexture2D ||
		!warp_IsFramebuffer)
		return false;

	/* A recreated GL context invalidates the old object name. */
	if (warp_framebuffer && !warp_IsFramebuffer (warp_framebuffer))
		warp_framebuffer = 0;
	if (!warp_framebuffer)
		warp_GenFramebuffers (1, &warp_framebuffer);

	return warp_framebuffer != 0;
}

typedef struct warp_render_state_s
{
	GLint framebuffer;
	GLint draw_buffer;
	GLint viewport[4];
	GLint scissor_box[4];
	GLint matrix_mode;
	GLboolean scissor_enabled;
	GLboolean cull_enabled;
	GLboolean color_mask[4];
} warp_render_state_t;

static void Warp_BeginTextureRendering (warp_render_state_t *state)
{
	glGetIntegerv (GL_FRAMEBUFFER_BINDING_EXT, &state->framebuffer);
	glGetIntegerv (GL_DRAW_BUFFER, &state->draw_buffer);
	glGetIntegerv (GL_VIEWPORT, state->viewport);
	glGetIntegerv (GL_SCISSOR_BOX, state->scissor_box);
	glGetIntegerv (GL_MATRIX_MODE, &state->matrix_mode);
	state->scissor_enabled = glIsEnabled (GL_SCISSOR_TEST);
	state->cull_enabled = glIsEnabled (GL_CULL_FACE);
	glGetBooleanv (GL_COLOR_WRITEMASK, state->color_mask);

	warp_BindFramebuffer (GL_FRAMEBUFFER_EXT, warp_framebuffer);
	glDrawBuffer (GL_COLOR_ATTACHMENT0_EXT);
	glViewport (0, 0, gl_warpimagesize, gl_warpimagesize);
	glDisable (GL_SCISSOR_TEST);
	glDisable (GL_CULL_FACE);
	glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glMatrixMode (GL_PROJECTION);
	glPushMatrix ();
	glLoadIdentity ();
	glOrtho (0, 128, 0, 128, -99999, 99999);
	glMatrixMode (GL_MODELVIEW);
	glPushMatrix ();
	glLoadIdentity ();
}

static void Warp_EndTextureRendering (const warp_render_state_t *state)
{
	glMatrixMode (GL_MODELVIEW);
	glPopMatrix ();
	glMatrixMode (GL_PROJECTION);
	glPopMatrix ();
	glMatrixMode (state->matrix_mode);

	warp_BindFramebuffer (GL_FRAMEBUFFER_EXT, (GLuint)state->framebuffer);
	glDrawBuffer ((GLenum)state->draw_buffer);
	glViewport (state->viewport[0], state->viewport[1],
		state->viewport[2], state->viewport[3]);
	glScissor (state->scissor_box[0], state->scissor_box[1],
		state->scissor_box[2], state->scissor_box[3]);
	if (state->scissor_enabled)
		glEnable (GL_SCISSOR_TEST);
	else
		glDisable (GL_SCISSOR_TEST);
	if (state->cull_enabled)
		glEnable (GL_CULL_FACE);
	else
		glDisable (GL_CULL_FACE);
	glColorMask (state->color_mask[0], state->color_mask[1],
		state->color_mask[2], state->color_mask[3]);
}

static qboolean Warp_LegacyCopyIsSafe (void)
{
	GLint samples = 0;

	if (vr_enabled.value || R_IsVRStereoFrame ())
		return false;

	glGetIntegerv (GL_SAMPLES, &samples);
	return samples == 0;
}

static qboolean Warp_AttachTexture (const texture_t *tx)
{
	warp_FramebufferTexture2D (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
		GL_TEXTURE_2D, tx->warpimage->texnum, 0);
	if (warp_CheckFramebufferStatus (GL_FRAMEBUFFER_EXT) == GL_FRAMEBUFFER_COMPLETE_EXT)
		return true;

	/* Never fall back to copying from a potentially multisampled eye target. */
	warp_FramebufferTexture2D (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
		GL_TEXTURE_2D, 0, 0);
	if (!warp_fbo_warning_printed)
	{
		Con_Warning ("Warp texture framebuffer is incomplete; retaining the previous liquid texture\n");
		warp_fbo_warning_printed = true;
	}
	return false;
}

/*
=============
R_UpdateWarpTextures -- johnfitz -- each frame, update warping textures
=============
*/
#ifdef __WATCOMC__ /* OW1.9 doesn't have floorf() */
#define floorf(_val)		(float)floor((_val))
#endif
void R_UpdateWarpTextures (void)
{
	const unsigned frame_marker = (unsigned)host_framecount + 1u;
	qmodel_t *model;
	texture_t *tx;
	int i, j;
	float x, y, x2, warptess;
	qboolean render_to_texture;
	qboolean legacy_copy;
	warp_render_state_t warp_state;

	if (cl.paused || r_drawflat_cheatsafe || r_lightmap_cheatsafe)
		return;

	warptess = 128.0f/CLAMP (3.0f, floorf(r_waterquality.value), 64.0f);
	GL_DisableMultitexture ();
	glEnable (GL_TEXTURE_2D);
	glDisable (GL_BLEND);
	glDisable (GL_ALPHA_TEST);
	glDisable (GL_DEPTH_TEST);
	glDepthMask (GL_FALSE);
	glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f (1, 1, 1, 1);

	/* Direct rendering is valid for desktop, desktop MSAA, and either VR FBO. */
	render_to_texture = Warp_InitFramebuffer ();
	legacy_copy = !render_to_texture && Warp_LegacyCopyIsSafe ();
	if (!render_to_texture && !legacy_copy)
	{
		if (!warp_fbo_warning_printed)
		{
			Con_Warning ("Framebuffer objects are required for liquid warping with MSAA or VR\n");
			warp_fbo_warning_printed = true;
		}
		goto cleanup;
	}
	if (render_to_texture)
		Warp_BeginTextureRendering (&warp_state);

	/*
	 * External brush models can own independent liquid textures.  Inline BSP
	 * models share the world's texture array, so skip them to avoid rendering
	 * the same texture repeatedly (especially once per submodel in VR).
	 */
	for (j = 1; j < MAX_MODELS; j++)
	{
		model = cl.model_precache[j];
		if (!model)
			continue;
		if (model->type != mod_brush || model->name[0] == '*')
			continue;

		for (i = 0; i < model->numtextures; i++)
		{
			if (!(tx = model->textures[i]) || !tx->update_warp || !tx->warpimage)
				continue;
			if (tx->warp_render_frame == frame_marker)
			{
				/* Shared/duplicate models and the second VR eye reuse this result. */
				tx->update_warp = false;
				continue;
			}

			//render warp
			if (render_to_texture && !Warp_AttachTexture (tx))
				continue;
			if (legacy_copy)
				GL_SetCanvas (CANVAS_WARPIMAGE);
			GL_Bind (tx->gltexture);
			for (x=0.0; x<128.0; x=x2)
			{
				x2 = x + warptess;
				glBegin (GL_TRIANGLE_STRIP);
				for (y=0.0; y<128.01; y+=warptess) // .01 for rounding errors
				{
					glTexCoord2f (WARPCALC(x,y), WARPCALC(y,x));
					glVertex2f (x,y);
					glTexCoord2f (WARPCALC(x2,y), WARPCALC(y,x2));
					glVertex2f (x2,y);
				}
				glEnd();
			}

			if (legacy_copy)
			{
				//copy to texture
				GL_Bind (tx->warpimage);
				glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, glx, gly+glheight-gl_warpimagesize, gl_warpimagesize, gl_warpimagesize);
			}
			else
			{
				/* Detach before generating mipmaps for the rendered texture. */
				warp_FramebufferTexture2D (GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
					GL_TEXTURE_2D, 0, 0);
				GL_Bind (tx->warpimage);
			}
			if (GL_GenerateMipmap)
				GL_GenerateMipmap (GL_TEXTURE_2D);

			tx->warp_render_frame = frame_marker;
			tx->update_warp = false;
		}
	}

	if (render_to_texture)
		Warp_EndTextureRendering (&warp_state);

cleanup:
	glDepthMask (GL_TRUE);
	glEnable (GL_DEPTH_TEST);
	glColor4f (1, 1, 1, 1);

	// ericw -- workaround for osx 10.6 driver bug when using FSAA. R_Clear only clears the warpimage part of the screen.
	GL_SetCanvas(CANVAS_DEFAULT);

	//if warp render went down into sbar territory, we need to be sure to refresh it next frame
	if (gl_warpimagesize + sb_lines > glheight)
		Sbar_Changed ();

	//if viewsize is less than 100, we need to redraw the frame around the viewport
	scr_tileclear_updates = 0;
}
