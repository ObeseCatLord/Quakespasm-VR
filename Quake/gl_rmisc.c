/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// r_misc.c

#include "quakedef.h"
#include "vr.h"

//johnfitz -- new cvars
extern cvar_t r_stereo;
extern cvar_t r_stereodepth;
extern cvar_t r_clearcolor;
extern cvar_t r_drawflat;
extern cvar_t r_flatlightstyles;
extern cvar_t r_lerplightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t gl_farclip;
extern cvar_t gl_overbright;
extern cvar_t gl_overbright_models;
extern cvar_t r_waterquality;
extern cvar_t r_useportalculling;
extern cvar_t r_oldwater;
extern cvar_t r_oldwater_max_drawpolys;
extern cvar_t r_waterwarp;
extern cvar_t r_oldskyleaf;
extern cvar_t r_drawworld;
extern cvar_t r_showtris;
extern cvar_t r_showbboxes;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_nolerp_list;
extern cvar_t r_noshadow_list;
extern cvar_t r_alias_batching;
extern cvar_t r_alphasort;
extern cvar_t r_perfdebug;
extern cvar_t r_perfdebug_min_ms;
extern cvar_t r_part_density;
extern cvar_t cl_coop_nametags;
//johnfitz
extern cvar_t gl_zfix; // QuakeSpasm z-fighting fix

extern gltexture_t *playertextures[MAX_SCOREBOARD]; //johnfitz

cvar_t	r_lightmapwide = {"r_lightmapwide","0",CVAR_ROM};


/*
====================
GL_Overbright_f -- johnfitz
====================
*/
static void GL_Overbright_f (cvar_t *var)
{
	R_RebuildAllLightmaps ();
}

/*
====================
GL_Fullbrights_f -- johnfitz
====================
*/
static void GL_Fullbrights_f (cvar_t *var)
{
	TexMgr_ReloadNobrightImages ();
}

/*
====================
R_SetClearColor_f -- johnfitz
====================
*/
static void R_SetClearColor_f (cvar_t *var)
{
	byte	*rgb;
	int		s;

	s = (int)r_clearcolor.value & 0xFF;
	rgb = (byte*)(d_8to24table + s);
	glClearColor (rgb[0]/255.0,rgb[1]/255.0,rgb[2]/255.0,0);
}

/*
===============
R_Model_ExtraFlags_List_f -- johnfitz -- called when r_nolerp_list or r_noshadow_list cvar changes
===============
*/
static void R_Model_ExtraFlags_List_f (cvar_t *var)
{
	int i;
	for (i=0; i < MAX_MODELS; i++)
		Mod_SetExtraFlags (cl.model_precache[i]);
}

/*
====================
R_SetWateralpha_f -- ericw
====================
*/
static void R_SetWateralpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel &&
		!(cl.worldmodel->contentstransparent & SURF_DRAWWATER) &&
		var->value < 1)
		Con_Warning("Map does not appear to be water-vised\n");
	map_wateralpha = var->value;
	map_fallbackalpha = var->value;
}

/*
====================
R_SetLavaalpha_f -- ericw
====================
*/
static void R_SetLavaalpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel &&
		!(cl.worldmodel->contentstransparent & SURF_DRAWLAVA) &&
		var->value && var->value < 1)
		Con_Warning("Map does not appear to be lava-vised\n");
	map_lavaalpha = var->value;
}

/*
====================
R_SetTelealpha_f -- ericw
====================
*/
static void R_SetTelealpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel &&
		!(cl.worldmodel->contentstransparent & SURF_DRAWTELE) &&
		var->value && var->value < 1)
		Con_Warning("Map does not appear to be tele-vised\n");
	map_telealpha = var->value;
}

/*
====================
R_SetSlimealpha_f -- ericw
====================
*/
static void R_SetSlimealpha_f (cvar_t *var)
{
	if (cls.signon == SIGNONS && cl.worldmodel &&
		!(cl.worldmodel->contentstransparent & SURF_DRAWSLIME) &&
		var->value && var->value < 1)
		Con_Warning("Map does not appear to be slime-vised\n");
	map_slimealpha = var->value;
}

/*
====================
GL_WaterAlphaForSurfface -- ericw
====================
*/
float GL_WaterAlphaForSurface (msurface_t *fa)
{
	if (fa->flags & SURF_DRAWLAVA)
		return map_lavaalpha > 0 ? map_lavaalpha : map_wateralpha;
	else if (fa->flags & SURF_DRAWTELE)
		return map_telealpha > 0 ? map_telealpha : map_wateralpha;
	else if (fa->flags & SURF_DRAWSLIME)
		return map_slimealpha > 0 ? map_slimealpha : map_wateralpha;
	else
		return map_wateralpha;
}


/*
===============
R_Init
===============
*/
void R_Init (void)
{
	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);

	Cvar_RegisterVariable (&r_norefresh);
	Cvar_RegisterVariable (&r_lightmap);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);
	Cvar_RegisterVariable (&r_shadows);
	Cvar_RegisterVariable (&r_wateralpha);
	Cvar_SetCallback (&r_wateralpha, R_SetWateralpha_f);
	Cvar_RegisterVariable (&r_useportalculling);
	Cvar_RegisterVariable (&r_litwater);
	Cvar_RegisterVariable (&r_dynamic);
	Cvar_RegisterVariable (&r_novis);
	Cvar_RegisterVariable (&r_gpuworldmark);
	Cvar_RegisterVariable (&r_gpuworldmark_validate);
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_perfdebug);
	Cvar_RegisterVariable (&r_perfdebug_min_ms);
	Cvar_RegisterVariable (&r_pos);

	Cvar_RegisterVariable (&gl_finish);
	Cvar_RegisterVariable (&gl_clear);
	Cvar_RegisterVariable (&gl_cull);
	Cvar_RegisterVariable (&gl_smoothmodels);
	Cvar_RegisterVariable (&gl_affinemodels);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_flashblend);
	Cvar_RegisterVariable (&gl_playermip);
	Cvar_RegisterVariable (&gl_nocolors);
	Cvar_RegisterVariable (&cl_coop_nametags);

	//johnfitz -- new cvars
	Cvar_RegisterVariable (&r_stereo);
	Cvar_RegisterVariable (&r_stereodepth);
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_SetCallback (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_waterquality);
	Cvar_RegisterVariable (&r_oldwater);
	Cvar_RegisterVariable (&r_oldwater_max_drawpolys);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_drawflat);
	Cvar_RegisterVariable (&r_flatlightstyles);
	Cvar_RegisterVariable (&r_lerplightstyles);
	Cvar_RegisterVariable (&r_oldskyleaf);
	Cvar_RegisterVariable (&r_drawworld);
	Cvar_RegisterVariable (&r_showtris);
	Cvar_RegisterVariable (&r_showbboxes);
	Cvar_RegisterVariable (&gl_farclip);
	Cvar_RegisterVariable (&gl_fullbrights);
	Cvar_RegisterVariable (&gl_overbright);
	Cvar_SetCallback (&gl_fullbrights, GL_Fullbrights_f);
	Cvar_SetCallback (&gl_overbright, GL_Overbright_f);
	Cvar_RegisterVariable (&gl_overbright_models);
	Cvar_RegisterVariable (&r_lerpmodels);
	Cvar_RegisterVariable (&r_lerpmove);
	Cvar_RegisterVariable (&r_alias_batching);
	Cvar_RegisterVariable (&r_alphasort);
	Cvar_RegisterVariable (&r_nolerp_list);
	Cvar_SetCallback (&r_nolerp_list, R_Model_ExtraFlags_List_f);
	Cvar_RegisterVariable (&r_noshadow_list);
	Cvar_SetCallback (&r_noshadow_list, R_Model_ExtraFlags_List_f);
	//johnfitz
	Cvar_RegisterVariable (&r_lightmapwide);
	Cvar_SetROM ("r_lightmapwide", gl_packed_pixels ? "1" : "0");

	Cvar_RegisterVariable (&gl_zfix); // QuakeSpasm z-fighting fix
	Cvar_RegisterVariable (&r_lavaalpha);
	Cvar_RegisterVariable (&r_telealpha);
	Cvar_RegisterVariable (&r_slimealpha);
	Cvar_RegisterVariable (&r_part_density);
	Cvar_RegisterVariable (&r_scale);
	Cvar_SetCallback (&r_lavaalpha, R_SetLavaalpha_f);
	Cvar_SetCallback (&r_telealpha, R_SetTelealpha_f);
	Cvar_SetCallback (&r_slimealpha, R_SetSlimealpha_f);

	R_InitParticles ();
#ifdef PSET_SCRIPT
	PScript_InitParticles ();
#endif
	R_SetClearColor_f (&r_clearcolor); //johnfitz

	Sky_Init (); //johnfitz
	Fog_Init (); //johnfitz
    VID_VR_Init(); //phoboslab
}

/*
===============
R_TranslatePlayerSkin -- johnfitz -- rewritten.  also, only handles new colors, not new skins
===============
*/
void R_TranslatePlayerSkin (int playernum)
{
	int			top, bottom;

	top = (cl.scores[playernum].colors & 0xf0)>>4;
	bottom = cl.scores[playernum].colors &15;

	//FIXME: if gl_nocolors is on, then turned off, the textures may be out of sync with the scoreboard colors.
	if (!gl_nocolors.value)
	{
		if (playertextures[playernum])
			TexMgr_ReloadImage (playertextures[playernum], top, bottom);
	}
}

/*
===============
R_TranslateNewPlayerSkin -- johnfitz -- split off of TranslatePlayerSkin -- this is called when
the skin or model actually changes, instead of just new colors
added bug fix from bengt jardup
===============
*/
void R_TranslateNewPlayerSkin (int playernum)
{
	char		name[64];
	byte		*pixels;
	aliashdr_t	*paliashdr;
	int		skinnum;

//get correct texture pixels
	currententity = &cl.entities[1+playernum];

	if (!currententity->model || currententity->model->type != mod_alias)
		return;

	paliashdr = (aliashdr_t *)Mod_Extradata (currententity->model);

	/*
	 * MD3 skins are already true-color textures, not the indexed texel block
	 * stored in an MDL alias header.  In particular, a native MD3 player
	 * model has no valid texels[] offsets to translate.  Leave its authored
	 * skin alone rather than treating the header as pixel data.
	 */
	if (paliashdr->poseverttype == ALIAS_POSE_MD3)
	{
		playertextures[playernum] = NULL;
		return;
	}

	skinnum = currententity->skinnum;

	//TODO: move these tests to the place where skinnum gets received from the server
	if (skinnum < 0 || skinnum >= paliashdr->numskins)
	{
		Con_DPrintf("(%d): Invalid player skin #%d\n", playernum, skinnum);
		skinnum = 0;
	}

	pixels = (byte *)paliashdr + paliashdr->texels[skinnum]; // This is not a persistent place!

//upload new image
	q_snprintf(name, sizeof(name), "player_%i", playernum);
	playertextures[playernum] = TexMgr_LoadImage (currententity->model, name, paliashdr->skinwidth, paliashdr->skinheight,
		SRC_INDEXED, pixels, paliashdr->gltextures[skinnum][0]->source_file, paliashdr->gltextures[skinnum][0]->source_offset, TEXPREF_PAD | TEXPREF_OVERWRITE);

//now recolor it
	R_TranslatePlayerSkin (playernum);
}

/*
===============
R_NewGame -- johnfitz -- handle a game switch
===============
*/
void R_NewGame (void)
{
	int i;

	R_InvalidateNoVisSurfaceCache ();

	//clear playertexture pointers (the textures themselves were freed by texmgr_newgame)
	for (i=0; i<MAX_SCOREBOARD; i++)
		playertextures[i] = NULL;
}

/*
=============
R_ParseWorldspawn

called at map load
=============
*/
static void R_ParseWorldspawn (void)
{
	char key[128], value[4096];
	const char *data;

	map_fallbackalpha = r_wateralpha.value;
	map_wateralpha = (cl.worldmodel->contentstransparent & SURF_DRAWWATER) ? r_wateralpha.value : 1;
	map_lavaalpha = (cl.worldmodel->contentstransparent & SURF_DRAWLAVA) ? r_lavaalpha.value : 1;
	map_telealpha = (cl.worldmodel->contentstransparent & SURF_DRAWTELE) ? r_telealpha.value : 1;
	map_slimealpha = (cl.worldmodel->contentstransparent & SURF_DRAWSLIME) ? r_slimealpha.value : 1;

	data = COM_Parse(cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
		return; // error

	while (1)
	{
		data = COM_Parse(data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			q_strlcpy(key, com_token + 1, sizeof(key));
		else
			q_strlcpy(key, com_token, sizeof(key));
		while (key[0] && key[strlen(key)-1] == ' ') // remove trailing spaces
			key[strlen(key)-1] = 0;
		data = COM_ParseEx(data, CPE_ALLOWTRUNC);
		if (!data)
			return; // error
		q_strlcpy(value, com_token, sizeof(value));

		if (!strcmp("wateralpha", key))
			map_wateralpha = atof(value);

		if (!strcmp("lavaalpha", key))
			map_lavaalpha = atof(value);

		if (!strcmp("telealpha", key))
			map_telealpha = atof(value);

		if (!strcmp("slimealpha", key))
			map_slimealpha = atof(value);
	}
}


/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	int		i;

	R_InvalidateNoVisSurfaceCache ();

	for (i=0 ; i<256 ; i++)
		d_lightstylevalue[i] = 264;		// normal light value

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
	for (i=0 ; i<cl.worldmodel->numleafs ; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = NULL;
	R_ClearParticles ();
#ifdef PSET_SCRIPT
	PScript_ClearParticles ();
#endif

	GL_BuildLightmaps ();
	GL_BuildBModelVertexBuffer ();
	//ericw -- no longer load alias models into a VBO here, it's done in Mod_LoadAliasModel

	r_framecount = 0; //johnfitz -- paranoid?
	r_visframecount = 0; //johnfitz -- paranoid?

	Sky_NewMap (); //johnfitz -- skybox in worldspawn
	Fog_NewMap (); //johnfitz -- global fog in worldspawn
	R_ParseWorldspawn (); //ericw -- wateralpha, lavaalpha, telealpha, slimealpha in worldspawn

	load_subdivide_size = gl_subdivide_size.value; //johnfitz -- is this the right place to set this?
}

/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int		i;
	float		start, stop, time;

	if (cls.state != ca_connected)
	{
		Con_Printf("Not connected to a server\n");
		return;
	}

	start = Sys_DoubleTime ();
	for (i = 0; i < 128; i++)
	{
		GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
		r_refdef.viewangles[1] = i/128.0*360.0;
		R_RenderView ();
		GL_EndRendering ();
	}

	glFinish ();
	stop = Sys_DoubleTime ();
	time = stop-start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128/time);
}

void D_FlushCaches (void)
{
}

static GLuint gl_programs[16];
static int gl_num_programs;

static qboolean GL_CheckShader (GLuint shader)
{
	GLint status;
	GL_GetShaderivFunc (shader, GL_COMPILE_STATUS, &status);

	if (status != GL_TRUE)
	{
		char infolog[1024];

		memset(infolog, 0, sizeof(infolog));
		GL_GetShaderInfoLogFunc (shader, sizeof(infolog), NULL, infolog);

		Con_Warning ("GLSL program failed to compile: %s", infolog);

		return false;
	}
	return true;
}

static qboolean GL_CheckProgram (GLuint program)
{
	GLint status;
	GL_GetProgramivFunc (program, GL_LINK_STATUS, &status);

	if (status != GL_TRUE)
	{
		char infolog[1024];

		memset(infolog, 0, sizeof(infolog));
		GL_GetProgramInfoLogFunc (program, sizeof(infolog), NULL, infolog);

		Con_Warning ("GLSL program failed to link: %s", infolog);

		return false;
	}
	return true;
}

static qboolean GL_CheckComputeShader (GLuint shader, const char *name)
{
	GLint status;
	GL_GetShaderivFunc (shader, GL_COMPILE_STATUS, &status);

	if (status != GL_TRUE)
	{
		char infolog[1024];

		memset(infolog, 0, sizeof(infolog));
		GL_GetShaderInfoLogFunc (shader, sizeof(infolog), NULL, infolog);

		Con_Warning ("GLSL compute shader '%s' failed to compile: %s", name, infolog);

		return false;
	}
	return true;
}

static qboolean GL_CheckComputeProgram (GLuint program, const char *name)
{
	GLint status;
	GL_GetProgramivFunc (program, GL_LINK_STATUS, &status);

	if (status != GL_TRUE)
	{
		char infolog[1024];

		memset(infolog, 0, sizeof(infolog));
		GL_GetProgramInfoLogFunc (program, sizeof(infolog), NULL, infolog);

		Con_Warning ("GLSL compute program '%s' failed to link: %s", name, infolog);

		return false;
	}
	return true;
}

/*
=============
GL_GetUniformLocation
=============
*/
GLint GL_GetUniformLocation (GLuint *programPtr, const char *name)
{
	GLint location;

	if (!programPtr)
		return -1;

	location = GL_GetUniformLocationFunc(*programPtr, name);
	if (location == -1)
	{
		Con_Warning("GL_GetUniformLocationFunc %s failed\n", name);
		*programPtr = 0;
	}
	return location;
}

/*
====================
GL_CreateProgram

Compiles and returns GLSL program.
====================
*/
GLuint GL_CreateProgram (const GLchar *vertSource, const GLchar *fragSource, int numbindings, const glsl_attrib_binding_t *bindings)
{
	int i;
	GLuint program, vertShader, fragShader;

	if (!gl_glsl_able)
		return 0;

	vertShader = GL_CreateShaderFunc (GL_VERTEX_SHADER);
	GL_ShaderSourceFunc (vertShader, 1, &vertSource, NULL);
	GL_CompileShaderFunc (vertShader);
	if (!GL_CheckShader (vertShader))
	{
		GL_DeleteShaderFunc (vertShader);
		return 0;
	}

	fragShader = GL_CreateShaderFunc (GL_FRAGMENT_SHADER);
	GL_ShaderSourceFunc (fragShader, 1, &fragSource, NULL);
	GL_CompileShaderFunc (fragShader);
	if (!GL_CheckShader (fragShader))
	{
		GL_DeleteShaderFunc (vertShader);
		GL_DeleteShaderFunc (fragShader);
		return 0;
	}

	program = GL_CreateProgramFunc ();
	GL_AttachShaderFunc (program, vertShader);
	GL_DeleteShaderFunc (vertShader);
	GL_AttachShaderFunc (program, fragShader);
	GL_DeleteShaderFunc (fragShader);

	for (i = 0; i < numbindings; i++)
	{
		GL_BindAttribLocationFunc (program, bindings[i].attrib, bindings[i].name);
	}

	GL_LinkProgramFunc (program);

	if (!GL_CheckProgram (program))
	{
		GL_DeleteProgramFunc (program);
		return 0;
	}
	else
	{
		if (gl_num_programs == Q_COUNTOF(gl_programs))
			Host_Error ("gl_programs overflow");

		gl_programs[gl_num_programs] = program;
		gl_num_programs++;

		return program;
	}
}

/*
====================
GL_CreateComputeProgram

Compiles and returns a GLSL compute program.
====================
*/
GLuint GL_CreateComputeProgram (const GLchar *source, const char *name)
{
	const char *label = name ? name : "unnamed";
	GLuint program, shader;

	if (!source || !gl_glsl_able ||
		gl_caps.compute_shader == gl_capability_unavailable ||
		!GL_DispatchComputeFunc)
		return 0;

	shader = GL_CreateShaderFunc (GL_COMPUTE_SHADER);
	if (!shader)
		return 0;

	GL_ShaderSourceFunc (shader, 1, &source, NULL);
	GL_CompileShaderFunc (shader);
	if (!GL_CheckComputeShader (shader, label))
	{
		GL_DeleteShaderFunc (shader);
		return 0;
	}

	program = GL_CreateProgramFunc ();
	if (!program)
	{
		GL_DeleteShaderFunc (shader);
		return 0;
	}

	GL_AttachShaderFunc (program, shader);
	GL_DeleteShaderFunc (shader);
	GL_LinkProgramFunc (program);

	if (!GL_CheckComputeProgram (program, label))
	{
		GL_DeleteProgramFunc (program);
		return 0;
	}

	if (gl_num_programs == Q_COUNTOF(gl_programs))
		Host_Error ("gl_programs overflow");

	gl_programs[gl_num_programs] = program;
	gl_num_programs++;

	return program;
}

/*
====================
R_DeleteShaders

Deletes any GLSL programs that have been created.
====================
*/
void R_DeleteShaders (void)
{
	int i;

	R_GPUWorldMarkContextLost();
	if (!gl_glsl_able)
		return;

	GL_UseProgram (0);

	for (i = 0; i < gl_num_programs; i++)
	{
		GL_DeleteProgramFunc (gl_programs[i]);
		gl_programs[i] = 0;
	}
	gl_num_programs = 0;
}

static GLuint current_program;

/*
====================
GL_UseProgram

glUseProgram wrapper
====================
*/
void GL_UseProgram (GLuint program)
{
	if (!GL_UseProgramFunc)
		return;

	if (current_program != program)
	{
		current_program = program;
		GL_UseProgramFunc (program);
	}
}

static GLuint current_array_buffer, current_element_array_buffer;

/*
====================
GL_BindBuffer

glBindBuffer wrapper
====================
*/
void GL_BindBuffer (GLenum target, GLuint buffer)
{
	GLuint *cache;

	if (!gl_vbo_able)
		return;

	switch (target)
	{
		case GL_ARRAY_BUFFER:
			cache = &current_array_buffer;
			break;
		case GL_ELEMENT_ARRAY_BUFFER:
			cache = &current_element_array_buffer;
			break;
		default:
			Host_Error("GL_BindBuffer: unsupported target %d", (int)target);
			return;
	}

	if (*cache != buffer)
	{
		*cache = buffer;
		GL_BindBufferFunc (target, *cache);
	}
}

/*
====================
GL_ClearBufferBindings

This must be called if you do anything that could make the cached bindings
invalid (e.g. manually binding, destroying the context).
====================
*/
void GL_ClearBufferBindings (void)
{
	if (!gl_vbo_able)
		return;

	current_array_buffer = 0;
	current_element_array_buffer = 0;
	GL_BindBufferFunc (GL_ARRAY_BUFFER, 0);
	GL_BindBufferFunc (GL_ELEMENT_ARRAY_BUFFER, 0);
}

/*
============================================================================
					FRAME RESOURCES
============================================================================

The renderer's compatibility paths cannot assume persistent mapping.  Keep
the first use deliberately small: opaque alias instance data gets a set of
three reusable array buffers.  On GL_ARB_buffer_storage systems these are
persistently mapped and protected by fences; elsewhere BufferData orphaning
plus BufferSubData retains the established streaming behavior.
*/

#define GL_FRAME_RESOURCE_COUNT 3
#define GL_FRAME_RESOURCE_BYTES (512 * 1024)
#define GL_FRAME_RESOURCE_ALIGN 16

typedef struct
{
	GLuint		buffer;
	GLubyte		*mapped;
	GLsync		fence;
} gl_frame_resource_t;

static gl_frame_resource_t gl_frame_resources[GL_FRAME_RESOURCE_COUNT];
static int gl_frame_resource_index;
static size_t gl_frame_resource_offset;
static qboolean gl_frame_resources_initialized;
static qboolean gl_frame_resources_active;
static qboolean gl_frame_resources_persistent;
gl_frame_resource_stats_t gl_frame_resource_stats;

static qboolean GL_FrameResources_HasPersistentSupport (void)
{
	return gl_caps.buffer_storage != gl_capability_unavailable &&
		GL_BufferStorageFunc && GL_MapBufferRangeFunc && GL_UnmapBufferFunc &&
		GL_FenceSyncFunc && GL_ClientWaitSyncFunc && GL_DeleteSyncFunc &&
		GL_MemoryBarrierFunc;
}

static void GL_FrameResources_Destroy (void)
{
	int i;

	for (i = 0; i < GL_FRAME_RESOURCE_COUNT; i++)
	{
		gl_frame_resource_t *resource = &gl_frame_resources[i];

		if (resource->fence && GL_DeleteSyncFunc)
		{
			GL_DeleteSyncFunc (resource->fence);
			resource->fence = NULL;
		}
		if (resource->mapped && GL_UnmapBufferFunc && resource->buffer)
		{
			GL_BindBuffer (GL_ARRAY_BUFFER, resource->buffer);
			if (!GL_UnmapBufferFunc (GL_ARRAY_BUFFER))
				Con_Warning ("Persistent alias instance buffer became corrupted while unmapping\n");
			resource->mapped = NULL;
		}
		if (resource->buffer && GL_DeleteBuffersFunc)
		{
			GLuint buffer = resource->buffer;
			GL_DeleteBuffersFunc (1, &buffer);
			resource->buffer = 0;
		}
	}
	GL_ClearBufferBindings ();
	gl_frame_resources_initialized = false;
	gl_frame_resources_active = false;
	gl_frame_resources_persistent = false;
	gl_frame_resource_index = 0;
	gl_frame_resource_offset = 0;
}

static qboolean GL_FrameResources_Create (void)
{
	int i;
	GLbitfield flags;

	if (gl_frame_resources_initialized)
		return true;
	if (!gl_vbo_able || !GL_GenBuffersFunc || !GL_DeleteBuffersFunc ||
		!GL_BindBufferFunc || !GL_BufferDataFunc || !GL_BufferSubDataFunc)
		return false;

	gl_frame_resources_persistent = GL_FrameResources_HasPersistentSupport ();
	flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
	for (i = 0; i < GL_FRAME_RESOURCE_COUNT; i++)
	{
		gl_frame_resource_t *resource = &gl_frame_resources[i];

		GL_GenBuffersFunc (1, &resource->buffer);
		if (!resource->buffer)
		{
			GL_FrameResources_Destroy ();
			return false;
		}
		GL_BindBuffer (GL_ARRAY_BUFFER, resource->buffer);
		if (gl_frame_resources_persistent)
		{
			GL_BufferStorageFunc (GL_ARRAY_BUFFER, GL_FRAME_RESOURCE_BYTES, NULL, flags);
			resource->mapped = (GLubyte *)GL_MapBufferRangeFunc (GL_ARRAY_BUFFER, 0,
				GL_FRAME_RESOURCE_BYTES, flags);
			if (!resource->mapped)
			{
				Con_Warning ("Persistent alias instance mapping failed; using orphaned uploads\n");
				GL_FrameResources_Destroy ();
				gl_frame_resources_persistent = false;
				break;
			}
		}
	}

	if (!gl_frame_resources_persistent && !gl_frame_resources_initialized)
	{
		/* A failed persistent map leaves immutable buffers behind.  Recreate
		 * conventional mutable buffers before enabling the fallback. */
		for (i = 0; i < GL_FRAME_RESOURCE_COUNT; i++)
		{
			gl_frame_resource_t *resource = &gl_frame_resources[i];
			if (!resource->buffer)
			{
				GL_GenBuffersFunc (1, &resource->buffer);
				if (!resource->buffer)
				{
					GL_FrameResources_Destroy ();
					return false;
				}
			}
		}
	}

	gl_frame_resources_initialized = true;
	GL_ClearBufferBindings ();
	Con_Printf ("Alias frame resources: mode=%s slots=%d slot_bytes=%d\n",
		gl_frame_resources_persistent ? "persistent-map" : "orphan-subdata",
		GL_FRAME_RESOURCE_COUNT, GL_FRAME_RESOURCE_BYTES);
	return true;
}

void GL_FrameResources_Begin (void)
{
	gl_frame_resource_t *resource;
	GLenum result;

	if (gl_frame_resources_active)
		return;
	memset (&gl_frame_resource_stats, 0, sizeof (gl_frame_resource_stats));
	if (!GL_FrameResources_Create ())
		return;

	resource = &gl_frame_resources[gl_frame_resource_index];
	if (gl_frame_resources_persistent && resource->fence)
	{
		result = GL_ClientWaitSyncFunc (resource->fence,
			GL_SYNC_FLUSH_COMMANDS_BIT, 0);
		if (result == GL_TIMEOUT_EXPIRED)
		{
			gl_frame_resource_stats.waits++;
			result = GL_ClientWaitSyncFunc (resource->fence,
				GL_SYNC_FLUSH_COMMANDS_BIT, (GLuint64)-1);
		}
		if (result == GL_WAIT_FAILED)
		{
			Con_Warning ("Alias frame-resource fence wait failed; forcing completion\n");
			glFinish ();
		}
		else if (result != GL_CONDITION_SATISFIED && result != GL_ALREADY_SIGNALED)
		{
			Con_Warning ("Alias frame-resource fence timed out; forcing completion\n");
			glFinish ();
		}
		GL_DeleteSyncFunc (resource->fence);
		resource->fence = NULL;
	}
	if (!gl_frame_resources_persistent)
	{
		GL_BindBuffer (GL_ARRAY_BUFFER, resource->buffer);
		GL_BufferDataFunc (GL_ARRAY_BUFFER, GL_FRAME_RESOURCE_BYTES, NULL, GL_STREAM_DRAW);
	}
	gl_frame_resource_offset = 0;
	gl_frame_resources_active = true;
}

void GL_FrameResources_End (void)
{
	gl_frame_resource_t *resource;

	if (!gl_frame_resources_active)
		return;
	resource = &gl_frame_resources[gl_frame_resource_index];
	if (gl_frame_resources_persistent)
	{
		resource->fence = GL_FenceSyncFunc (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		if (!resource->fence)
		{
			Con_Warning ("Alias frame-resource fence creation failed; forcing completion\n");
			glFinish ();
		}
	}
	if (++gl_frame_resource_index == GL_FRAME_RESOURCE_COUNT)
		gl_frame_resource_index = 0;
	gl_frame_resources_active = false;
}

void GL_FrameResources_Shutdown (void)
{
	if (gl_frame_resources_initialized)
		GL_FrameResources_Destroy ();
}

qboolean GL_FrameResources_UploadAliasInstances (const void *data, size_t size,
	GLuint *buffer, GLintptr *offset)
{
	size_t aligned;
	gl_frame_resource_t *resource;

	if (!data || !buffer || !offset || size == 0)
		return false;
	if (!gl_frame_resources_active)
		GL_FrameResources_Begin ();
	if (!gl_frame_resources_active)
		return false;

	if (gl_frame_resource_offset > SIZE_MAX - (GL_FRAME_RESOURCE_ALIGN - 1))
		return false;
	aligned = (gl_frame_resource_offset + (GL_FRAME_RESOURCE_ALIGN - 1)) &
		~(size_t)(GL_FRAME_RESOURCE_ALIGN - 1);
	if (aligned > GL_FRAME_RESOURCE_BYTES || size > GL_FRAME_RESOURCE_BYTES - aligned)
	{
		gl_frame_resource_stats.wraps++;
		return false;
	}

	resource = &gl_frame_resources[gl_frame_resource_index];
	if (gl_frame_resources_persistent)
	{
		memcpy (resource->mapped + aligned, data, size);
		GL_MemoryBarrierFunc (GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
	}
	else
	{
		GL_BindBuffer (GL_ARRAY_BUFFER, resource->buffer);
		GL_BufferSubDataFunc (GL_ARRAY_BUFFER, aligned, size, data);
		gl_frame_resource_stats.fallback_uploads++;
	}
	*buffer = resource->buffer;
	*offset = (GLintptr)aligned;
	gl_frame_resource_offset = aligned + size;
	gl_frame_resource_stats.upload_bytes += size;
	if (gl_frame_resource_offset > gl_frame_resource_stats.high_water_bytes)
		gl_frame_resource_stats.high_water_bytes = gl_frame_resource_offset;
	return true;
}

void GL_FrameResources_RecordFallbackUpload (size_t size)
{
	gl_frame_resource_stats.fallback_uploads++;
	gl_frame_resource_stats.upload_bytes += size;
}
