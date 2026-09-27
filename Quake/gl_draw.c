/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

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

// draw.c -- 2d drawing

#include "quakedef.h"

extern cvar_t scr_style;

cvar_t scr_conalpha = {"scr_conalpha", "0.5", CVAR_ARCHIVE}; // johnfitz
// 0 = nearest, 1 = linear, 2 = xBR Level 2. World texture filtering remains independent.
cvar_t scr_guifilter = {"scr_guifilter", "0", CVAR_ARCHIVE};

qpic_t *draw_disc;
qpic_t *draw_backtile;

gltexture_t *char_texture;		// johnfitz
qpic_t		*pic_ovr, *pic_ins; // johnfitz -- new cursor handling
qpic_t		*pic_nul;			// johnfitz -- for missing gfx, don't crash

// johnfitz -- new pics
static const byte pic_ovr_data[8][8] = {
	{255, 255, 255, 255, 255, 255, 255, 255}, {255, 15, 15, 15, 15, 15, 15, 255}, {255, 15, 15, 15, 15, 15, 15, 2}, {255, 15, 15, 15, 15, 15, 15, 2},
	{255, 15, 15, 15, 15, 15, 15, 2},		  {255, 15, 15, 15, 15, 15, 15, 2},	  {255, 15, 15, 15, 15, 15, 15, 2}, {255, 255, 2, 2, 2, 2, 2, 2},
};

static const byte pic_ins_data[9][8] = {
	{15, 15, 255, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255},
	{15, 15, 2, 255, 255, 255, 255, 255},	{15, 15, 2, 255, 255, 255, 255, 255}, {15, 15, 2, 255, 255, 255, 255, 255},
	{15, 15, 2, 255, 255, 255, 255, 255},	{15, 15, 2, 255, 255, 255, 255, 255}, {255, 2, 2, 255, 255, 255, 255, 255},
};

static const byte pic_nul_data[8][8] = {
	{252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0}, {252, 252, 252, 252, 0, 0, 0, 0},
	{0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252}, {0, 0, 0, 0, 252, 252, 252, 252},
};

#if 0 // vso - unused
static const byte pic_stipple_data[8][8] = {
	{255, 0, 0, 0, 255, 0, 0, 0}, {0, 0, 255, 0, 0, 0, 255, 0}, {255, 0, 0, 0, 255, 0, 0, 0}, {0, 0, 255, 0, 0, 0, 255, 0},
	{255, 0, 0, 0, 255, 0, 0, 0}, {0, 0, 255, 0, 0, 0, 255, 0}, {255, 0, 0, 0, 255, 0, 0, 0}, {0, 0, 255, 0, 0, 0, 255, 0},
};

static const byte pic_crosshair_data[8][8] = {
	{255, 255, 255, 255, 255, 255, 255, 255},
	{255, 255, 255, 8, 9, 255, 255, 255},
	{255, 255, 255, 6, 8, 2, 255, 255},
	{255, 6, 8, 8, 6, 8, 8, 255},
	{255, 255, 2, 8, 8, 2, 2, 2},
	{255, 255, 255, 7, 8, 2, 255, 255},
	{255, 255, 255, 255, 2, 2, 255, 255},
	{255, 255, 255, 255, 255, 255, 255, 255},
};
#endif
// johnfitz

typedef struct
{
	gltexture_t *gltexture;
	float		 sl, tl, sh, th;
} glpic_t;

//==============================================================================
//
//  PIC CACHING
//
//==============================================================================
typedef struct cachepic_s
{
	// dynamically-allocated chained cachepic_t
	struct cachepic_s *next;
	char			   name[MAX_QPATH];
	int				   picflags;
	qpic_t			   pic;
	byte			   padding[32];
} cachepic_t;

// For appended glpic paylaod, padding MUST by placed just AFTER pic and being of enough size to hold glpic_t.
// (Technically the total padding size is (pic.data[4] + padding[32]) but we discard pic.data size in this check)
COMPILE_TIME_ASSERT ("cachepic padding placement", offsetof (cachepic_t, padding) == offsetof (cachepic_t, pic) + sizeof (((struct cachepic_s *)0)->pic));
COMPILE_TIME_ASSERT ("cachepic padding size", sizeof (((struct cachepic_s *)0)->padding) >= sizeof (glpic_t));

// draw_qcvm_mutex also protects q_cachepics  / scrap updates
extern SDL_Mutex  *draw_qcvm_mutex;
static cachepic_t *q_cachepics;

// last entry of the chained q_cachepics, new entries are appended to it
static cachepic_t *q_cachepics_last_entry;

// Fast lookup pic name => cachepic_t* for q_cachepics.
static hash_map_t *q_cachepics_map;

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

#define MAX_SCRAPS	 2
#define BLOCK_WIDTH	 256
#define BLOCK_HEIGHT 256

int			 scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		 scrap_texels[MAX_SCRAPS][BLOCK_WIDTH * BLOCK_HEIGHT]; // johnfitz -- removed *4 after BLOCK_HEIGHT
qboolean	 scrap_dirty;
gltexture_t *scrap_textures[MAX_SCRAPS]; // johnfitz

/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
static int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int i, j;
	int best, best2;
	int texnum;

	for (texnum = 0; texnum < MAX_SCRAPS; texnum++)
	{
		best = BLOCK_HEIGHT;

		for (i = 0; i < BLOCK_WIDTH - w; i++)
		{
			best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (scrap_allocated[texnum][i + j] >= best)
					break;
				if (scrap_allocated[texnum][i + j] > best2)
					best2 = scrap_allocated[texnum][i + j];
			}
			if (j == w)
			{ // this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > BLOCK_HEIGHT)
			continue;

		for (i = 0; i < w; i++)
			scrap_allocated[texnum][*x + i] = best + h;

		return texnum;
	}

	Sys_Error ("Scrap_AllocBlock: full");
	return 0;
}

/*
================
Scrap_Upload -- johnfitz -- now uses TexMgr
================
*/
static void Scrap_Upload (void)
{
	char name[8];
	int	 i;

	for (i = 0; i < MAX_SCRAPS; i++)
	{
		q_snprintf (name, sizeof (name), "scrap%i", i);
		scrap_textures[i] = TexMgr_LoadImage (
			NULL, name, BLOCK_WIDTH, BLOCK_HEIGHT, SRC_INDEXED, scrap_texels[i], "", (src_offset_t)scrap_texels[i],
			TEXPREF_ALPHA | TEXPREF_OVERWRITE | TEXPREF_NOPICMIP);
	}

	scrap_dirty = false;
}

/*
================
Draw_PicFromWad
================
*/
qpic_t *Draw_PicFromWad2 (const char *name, unsigned int texflags, int picflags)
{
	int			 i;
	qpic_t		*p;
	cachepic_t	*pic;
	glpic_t		 gl;
	src_offset_t offset; // johnfitz
	lumpinfo_t	*info;

	// Fast lookup:
	p = Draw_GetCachedPic (name);

	if (p)
		return p;

	// not cached, searched for it:
	p = (qpic_t *)W_GetLumpName (name, &info);

	if (!p)
	{
		Con_Warning ("W_GetLumpName: %s not found\n", name);
		return pic_nul; // johnfitz
	}
	if (info->type != TYP_QPIC)
	{
		// can be another format that QPIC (.lmp), ex. png, tga, jpg, pcx , this is not an error at that point
		Con_DPrintf ("Draw_PicFromWad: lump \"%s\" is not a qpic\n", name);
		return pic_nul; // johnfitz
	}

	// We have TYP_QPIC, check its basic characteristics:
	if (info->size < (int)(sizeof (int) * 2) || sizeof (int) * 2 + p->width * p->height > (size_t)info->size)
	{
		Con_Warning ("Draw_PicFromWad: pic \"%s\" truncated\n", name);
		return pic_nul; // johnfitz
	}

	if (p->width < 0 || p->height < 0)
	{
		Con_Warning ("Draw_PicFromWad: bad size (%dx%d) for pic \"%s\"\n", p->width, p->height, name);
		return pic_nul; // johnfitz
	}

	// load little ones into the scrap
	if (p->width < 64 && p->height < 64)
	{
		int	  x = 0, y = 0;
		int	  j, k;
		int	  texnum;
		byte *data = p->data;

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y);
		scrap_dirty = true;
		k = 0;
		for (i = 0; i < p->height; i++)
		{
			for (j = 0; j < p->width; j++, k++)
				scrap_texels[texnum][(y + i) * BLOCK_WIDTH + x + j] = data[k];
		}
		gl.gltexture = scrap_textures[texnum]; // johnfitz -- changed to an array
		// johnfitz -- no longer go from 0.01 to 0.99
		gl.sl = x / (float)BLOCK_WIDTH;
		gl.sh = (x + p->width) / (float)BLOCK_WIDTH;
		gl.tl = y / (float)BLOCK_WIDTH;
		gl.th = (y + p->height) / (float)BLOCK_WIDTH;
	}
	else
	{
		char texturename[64];														// johnfitz
		q_snprintf (texturename, sizeof (texturename), "%s:%s", WADFILENAME, name); // johnfitz

		offset = (src_offset_t)p - (src_offset_t)wad_base + sizeof (int) * 2; // johnfitz

		gl.gltexture = TexMgr_LoadImage (NULL, texturename, p->width, p->height, SRC_INDEXED, p->data, WADFILENAME, offset, texflags); // johnfitz -- TexMgr
		gl.sl = 0;
		gl.sh = 1;
		gl.tl = 0;
		gl.th = 1;
	}

	// Create a new pic:
	pic = Mem_Alloc (sizeof (*pic));
	pic->picflags = picflags;

	q_strlcpy (pic->name, name, countof (pic->name));
	pic->pic = *p;

	memcpy ((void *)&(pic->pic.data), &gl, sizeof (glpic_t));

	// Add to cache:
	assert (pic->next == NULL);

	if (!q_cachepics)
	{
		q_cachepics = pic;
		q_cachepics_last_entry = pic;
	}
	else
	{
		q_cachepics_last_entry->next = pic;
		q_cachepics_last_entry = pic;
	}

	// we must degrade pic->name to a (const char*) because this is what the hashmap expects (8 bytes pointer)
	const char *pic_name_as_pointer = &pic->name[0];
	HashMap_Insert (q_cachepics_map, &pic_name_as_pointer, &pic);

	return &pic->pic;
}

qpic_t *Draw_PicFromWad (const char *name)
{
	return Draw_PicFromWad2 (name, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP, PICFLAG_AUTO);
}
#if 0 // vso - unused 
static qpic_t *Draw_GetCachedPic (const char *path)
{
	cachepic_t *pic;
	int			i;

	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	return NULL;
}
#endif
/*
================
Draw_GetCachedPic : get a pic from cache if already present, or return NULL if not.
================
*/
qpic_t *Draw_GetCachedPic (const char *path)
{
	// Fast lookup:
	cachepic_t **pic_ptr = HashMap_Lookup (cachepic_t *, q_cachepics_map, &path);

	// found
	if (pic_ptr)
	{
		return &((*pic_ptr)->pic);
	}

	return NULL;
}

/*
================
Draw_CachePic
================
*/
qpic_t *Draw_TryCachePic (const char *path, unsigned int texflags, int picflags)
{
	qpic_t	   *p;
	cachepic_t *pic;
	glpic_t		gl;

	// Fast lookup:
	p = Draw_GetCachedPic (path);

	if (p)
		return p;

	//
	// load the pic from disk
	//
	unsigned int   pic_width = 0;
	unsigned int   pic_height = 0;
	void		  *pic_data = NULL;
	//
	enum srcformat pic_fmt = SRC_INDEXED;

	// Image_LoadImage works without file extensions.
	char npath[MAX_QPATH];
	COM_StripExtension (path, npath, sizeof (npath));

	pic_data = Image_LoadImage (npath, (int *)&pic_width, (int *)&pic_height, &pic_fmt, 0);

	if (!pic_data)
	{
		return NULL;
	}

	// Create a new pic:
	pic = Mem_Alloc (sizeof (*pic));
	pic->picflags = picflags;

	q_strlcpy (pic->name, path, countof (pic->name));

	pic->pic.width = pic_width;
	pic->pic.height = pic_height;

	// pass the extensionless name as the source so TexMgr_ReloadImage can find the image
	// again through Image_LoadImage (needed to recolor gfx/menuplyr.lmp in the setup menu)
	gl.gltexture = TexMgr_LoadImage (NULL, path, pic_width, pic_height, pic_fmt, pic_data, npath, 0, texflags | TEXPREF_NOPICMIP); // johnfitz -- TexMgr

	// those are always normalized coordinates
	gl.sl = 0;
	gl.sh = 1;
	gl.tl = 0;
	gl.th = 1;

	memcpy ((void *)&(pic->pic.data), &gl, sizeof (glpic_t));

	// Add to cache:
	assert (pic->next == NULL);

	if (!q_cachepics)
	{
		q_cachepics = pic;
		q_cachepics_last_entry = pic;
	}
	else
	{
		q_cachepics_last_entry->next = pic;
		q_cachepics_last_entry = pic;
	}

	// we must degrade pic->name to a (const char*) because this is what the hashmap expects (8 bytes pointer)
	const char *pic_name_as_pointer = &pic->name[0];
	HashMap_Insert (q_cachepics_map, &pic_name_as_pointer, &pic);

	Mem_Free (pic_data);

	return &pic->pic;
}

qpic_t *Draw_CachePic (const char *path)
{
	qpic_t *pic = Draw_TryCachePic (path, TEXPREF_ALPHA | TEXPREF_PAD | TEXPREF_NOPICMIP, PICFLAG_AUTO);
	if (!pic)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	return pic;
}

/*
================
Draw_MakePic -- johnfitz -- generate pics from internal data
================
*/
static qpic_t *Draw_MakePic (const char *name, int width, int height, const byte *data)
{
	int		flags = TEXPREF_NEAREST | TEXPREF_ALPHA | TEXPREF_PERSIST | TEXPREF_NOPICMIP | TEXPREF_PAD;
	qpic_t *pic;
	glpic_t gl;

	pic = (qpic_t *)Mem_Alloc (sizeof (qpic_t) - 4 + sizeof (glpic_t));
	pic->width = width;
	pic->height = height;

	gl.gltexture = TexMgr_LoadImage (NULL, name, width, height, SRC_INDEXED, (byte *)data, "", (src_offset_t)data, flags);
	gl.sl = 0;
	gl.sh = 1;
	gl.tl = 0;
	gl.th = 1;

	memcpy ((void *)&(pic->data), &gl, sizeof (glpic_t));

	return pic;
}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
===============
Draw_LoadPics -- johnfitz
===============
*/
static void Draw_LoadPics (void)
{
	byte		*data;
	src_offset_t offset;
	lumpinfo_t	*info;

	data = (byte *)W_GetLumpName ("conchars", &info);
	if (!data)
		Sys_Error ("Draw_LoadPics: couldn't load conchars");
	offset = (src_offset_t)data - (src_offset_t)wad_base;
	char_texture = TexMgr_LoadImage (
		NULL, WADFILENAME ":conchars", 128, 128, SRC_INDEXED, data, WADFILENAME, offset, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_NOPICMIP | TEXPREF_CONCHARS);

	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
}

/*
===============
Draw_NewGame -- johnfitz
===============
*/
void Draw_NewGame (void)
{
	SDL_LockMutex (draw_qcvm_mutex);

	// empty scrap and reallocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_texels, 255, sizeof (scrap_texels));

	Scrap_Upload (); // creates 2 empty gltextures

	// empty pic cache :
	cachepic_t *cached_pic = q_cachepics;
	cachepic_t *next_cached_pic;

	while (cached_pic)
	{
		next_cached_pic = cached_pic->next;
		Mem_Free (cached_pic);
		cached_pic = next_cached_pic;
	}
	q_cachepics = NULL;
	q_cachepics_last_entry = NULL;

	HashMap_Clear (q_cachepics_map);

	// reload wad pics
	W_LoadWadFile (); // johnfitz -- filename is now hard-coded for honesty
	Draw_LoadPics ();
	SCR_LoadPics ();
	Sbar_LoadPics ();

	SDL_UnlockMutex (draw_qcvm_mutex);
}

/*
===============
Draw_Init -- johnfitz -- rewritten
===============
*/
void Draw_Init (void)
{
	q_cachepics_map = HashMap_Create (const char *, cachepic_t *, &HashStr, &HashStrCmp);

	Cvar_RegisterVariable (&scr_conalpha);
	Cvar_RegisterVariable (&scr_guifilter);

	// clear scrap and allocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_texels, 255, sizeof (scrap_texels));

	Scrap_Upload (); // creates 2 empty textures

	// create internal pics
	pic_ins = Draw_MakePic ("ins", 8, 9, &pic_ins_data[0][0]);
	pic_ovr = Draw_MakePic ("ovr", 8, 8, &pic_ovr_data[0][0]);
	pic_nul = Draw_MakePic ("nul", 8, 8, &pic_nul_data[0][0]);

	// load game pics
	Draw_LoadPics ();
}

//==============================================================================
//
//  2D DRAWING
//
//==============================================================================

static float canvas_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

/*
================
GL_SetCanvasColor
================
*/
void GL_SetCanvasColor (float r, float g, float b, float a)
{
	canvas_color[0] = r;
	canvas_color[1] = g;
	canvas_color[2] = b;
	canvas_color[3] = a;
}

/*
================
Draw_FillCharacterQuad
================
*/
static void Draw_FillCharacterQuadScaled (float x, float y, float scale, char num, draw_pic_vertex_t *output, int rotation)
{
	const int	glyph = (unsigned char)num;
	const int	row = glyph >> 4;
	const int	col = glyph & 15;
	const float st_size = 1.0f / 16.0f;
	// Fixes sampling into previous/next character because of float rounding
	const float texel_offset = 0.001f;
	const float frow = row * st_size;
	const float fcol = col * st_size;

	draw_pic_vertex_t corner_verts[4];
	memset (&corner_verts, 255, sizeof (corner_verts));

	const float size = CHARACTER_SIZE * scale;
	float		texcoords[4][2] = {
		  {x, y},
		  {x + size, y},
		  {x + size, y + size},
		  {x, y + size},
	  };

	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < 4; ++j)
			corner_verts[i].color[j] = (byte)(canvas_color[j] * 255.0f);
		corner_verts[i].texture_region[0] = fcol;
		corner_verts[i].texture_region[1] = frow;
		corner_verts[i].texture_region[2] = fcol + st_size;
		corner_verts[i].texture_region[3] = frow + st_size;
	}

	corner_verts[0].position[0] = texcoords[(rotation + 0) % 4][0];
	corner_verts[0].position[1] = texcoords[(rotation + 0) % 4][1];
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texcoord[0] = fcol + texel_offset;
	corner_verts[0].texcoord[1] = frow + texel_offset;

	corner_verts[1].position[0] = texcoords[(rotation + 1) % 4][0];
	corner_verts[1].position[1] = texcoords[(rotation + 1) % 4][1];
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texcoord[0] = fcol + st_size - texel_offset;
	corner_verts[1].texcoord[1] = frow + texel_offset;

	corner_verts[2].position[0] = texcoords[(rotation + 2) % 4][0];
	corner_verts[2].position[1] = texcoords[(rotation + 2) % 4][1];
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texcoord[0] = fcol + st_size - texel_offset;
	corner_verts[2].texcoord[1] = frow + st_size - texel_offset;

	corner_verts[3].position[0] = texcoords[(rotation + 3) % 4][0];
	corner_verts[3].position[1] = texcoords[(rotation + 3) % 4][1];
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texcoord[0] = fcol + texel_offset;
	corner_verts[3].texcoord[1] = frow + st_size - texel_offset;

	output[0] = corner_verts[0];
	output[1] = corner_verts[1];
	output[2] = corner_verts[2];
	output[3] = corner_verts[2];
	output[4] = corner_verts[3];
	output[5] = corner_verts[0];
}

static void Draw_FillCharacterQuad (float x, float y, char num, draw_pic_vertex_t *output, int rotation)
{
	Draw_FillCharacterQuadScaled (x, y, 1.0f, num, output, rotation);
}

typedef enum
{
	DRAW_FILTER_NEAREST,
	DRAW_FILTER_LINEAR,
	DRAW_FILTER_XBR
} draw_filter_t;

static draw_filter_t Draw_GetPicFilter (void)
{
	switch ((int)scr_guifilter.value)
	{
	case 1:
		return DRAW_FILTER_LINEAR;
	case 2:
		return DRAW_FILTER_XBR;
	default:
		return DRAW_FILTER_NEAREST;
	}
}

static draw_filter_t Draw_GetTextFilter (void)
{
	return (int)scr_guifilter.value == 2 ? DRAW_FILTER_XBR : DRAW_FILTER_NEAREST;
}

static void Draw_BindPicState (cb_context_t *cbx, gltexture_t *texture, qboolean alpha_blend, draw_filter_t filter)
{
	if (filter == DRAW_FILTER_XBR)
		R_BindGraphicsPipeline (cbx, alpha_blend ? PIPELINE_MENU_XBR_BLEND : PIPELINE_MENU_XBR);
	else
		R_BindGraphicsPipeline (cbx, alpha_blend ? PIPELINE_GUI_BLEND : PIPELINE_GUI);

	VkDescriptorSet descriptor_sets[2] = {texture->descriptor_set, vulkan_globals.gui_sampler_descriptor_sets[filter == DRAW_FILTER_LINEAR ? 1 : 0]};
	vkCmdBindDescriptorSets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cbx->current_pipeline.layout.handle, 0, countof (descriptor_sets), descriptor_sets, 0, NULL);
}

/*
================
Draw_Character
================
*/
void Draw_Character (cb_context_t *cbx, float x, float y, int num)
{
	if (y <= -CHARACTER_SIZE)
		return; // totally off screen

	const int rotation = (num / 256) % 4;
	num &= 255;

	if (num == 32)
		return; // don't waste verts on spaces

	VkBuffer		   buffer;
	VkDeviceSize	   buffer_offset;
	draw_pic_vertex_t *vertices = (draw_pic_vertex_t *)R_VertexAllocate (6 * sizeof (*vertices), &buffer, &buffer_offset);
	Draw_FillCharacterQuad (x, y, (char)num, vertices, rotation);

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	Draw_BindPicState (cbx, char_texture, canvas_color[3] < 1.0f, Draw_GetTextFilter ());
	vulkan_globals.vk_cmd_draw (cbx->cb, 6, 1, 0, 0);
}

/*
================
Draw_String
================
*/
void Draw_String (cb_context_t *cbx, float x, float y, const char *str)
{
	int			num_verts = 0;
	int			i;
	const char *tmp;

	if (y <= -CHARACTER_SIZE)
		return; // totally off screen

	for (tmp = str; *tmp != 0; ++tmp)
		if (*tmp != 32)
			num_verts += 6;

	VkBuffer		   buffer;
	VkDeviceSize	   buffer_offset;
	draw_pic_vertex_t *vertices = (draw_pic_vertex_t *)R_VertexAllocate (num_verts * sizeof (*vertices), &buffer, &buffer_offset);

	for (i = 0; *str != 0; ++str)
	{
		if (*str != 32)
		{
			Draw_FillCharacterQuad (x, y, *str, vertices + i * 6, 0);
			i++;
		}
		x += CHARACTER_SIZE;
	}

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	Draw_BindPicState (cbx, char_texture, canvas_color[3] < 1.0f, Draw_GetTextFilter ());
	vulkan_globals.vk_cmd_draw (cbx->cb, num_verts, 1, 0, 0);
}

/*
================
Draw_String_Scaled
================
*/
void Draw_String_Scaled (cb_context_t *cbx, float x, float y, const char *str, float scale)
{
	int			num_verts = 0;
	int			i;
	const char *tmp;
	const float size = CHARACTER_SIZE * scale;

	if (y <= -size)
		return;

	for (tmp = str; *tmp != 0; ++tmp)
		if (*tmp != 32)
			num_verts += 6;

	VkBuffer		   buffer;
	VkDeviceSize	   buffer_offset;
	draw_pic_vertex_t *vertices = (draw_pic_vertex_t *)R_VertexAllocate (num_verts * sizeof (*vertices), &buffer, &buffer_offset);

	for (i = 0; *str != 0; ++str)
	{
		if (*str != 32)
		{
			Draw_FillCharacterQuadScaled (x, y, scale, *str, vertices + i * 6, 0);
			i++;
		}
		x += size;
	}

	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	Draw_BindPicState (cbx, char_texture, canvas_color[3] < 1.0f, Draw_GetTextFilter ());
	vulkan_globals.vk_cmd_draw (cbx->cb, num_verts, 1, 0, 0);
}

/*
=============
Draw_Pic -- johnfitz -- modified
=============
 */
void Draw_Pic (cb_context_t *cbx, float x, float y, qpic_t *pic, float alpha, qboolean alpha_blend)
{
	glpic_t gl;
	int		i;

	if (scrap_dirty)
		Scrap_Upload ();
	memcpy (&gl, pic->data, sizeof (glpic_t));

	VkBuffer		   buffer;
	VkDeviceSize	   buffer_offset;
	draw_pic_vertex_t *vertices = (draw_pic_vertex_t *)R_VertexAllocate (6 * sizeof (*vertices), &buffer, &buffer_offset);

	draw_pic_vertex_t corner_verts[4];
	memset (&corner_verts, 255, sizeof (corner_verts));

	corner_verts[0].position[0] = x;
	corner_verts[0].position[1] = y;
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texcoord[0] = gl.sl;
	corner_verts[0].texcoord[1] = gl.tl;

	corner_verts[1].position[0] = x + pic->width;
	corner_verts[1].position[1] = y;
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texcoord[0] = gl.sh;
	corner_verts[1].texcoord[1] = gl.tl;

	corner_verts[2].position[0] = x + pic->width;
	corner_verts[2].position[1] = y + pic->height;
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texcoord[0] = gl.sh;
	corner_verts[2].texcoord[1] = gl.th;

	corner_verts[3].position[0] = x;
	corner_verts[3].position[1] = y + pic->height;
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texcoord[0] = gl.sl;
	corner_verts[3].texcoord[1] = gl.th;

	for (i = 0; i < 4; ++i)
	{
		corner_verts[i].color[3] = alpha * 255.0f;
		corner_verts[i].texture_region[0] = gl.sl;
		corner_verts[i].texture_region[1] = gl.tl;
		corner_verts[i].texture_region[2] = gl.sh;
		corner_verts[i].texture_region[3] = gl.th;
	}

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vkCmdBindVertexBuffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	Draw_BindPicState (cbx, gl.gltexture, alpha_blend, Draw_GetPicFilter ());
	vkCmdDraw (cbx->cb, 6, 1, 0, 0);
}

static void Draw_SubPicInternal (
	cb_context_t *cbx, float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2, float *rgb, float alpha, qboolean force_linear,
	qboolean force_blend)
{
	glpic_t	 gl;
	qboolean alpha_blend = force_blend || alpha < 1.0f;
	int		 i;
	if (alpha <= 0.0f)
		return;

	s2 += s1;
	t2 += t1;

	if (scrap_dirty)
		Scrap_Upload ();
	memcpy (&gl, pic->data, sizeof (glpic_t));
	if (!gl.gltexture)
		return;

	vec4_t rgba = {255.0f, 255.0f, 255.0f, 255.0f};
	if (rgb)
	{
		for (i = 0; i < 3; i++)
			rgba[i] *= rgb[i];
	}
	rgba[3] *= alpha;

	VkBuffer		   buffer;
	VkDeviceSize	   buffer_offset;
	draw_pic_vertex_t *vertices = (draw_pic_vertex_t *)R_VertexAllocate (6 * sizeof (*vertices), &buffer, &buffer_offset);

	draw_pic_vertex_t corner_verts[4];
	memset (&corner_verts, 255, sizeof (corner_verts));

	corner_verts[0].position[0] = x;
	corner_verts[0].position[1] = y;
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texcoord[0] = gl.sl * (1 - s1) + s1 * gl.sh;
	corner_verts[0].texcoord[1] = gl.tl * (1 - t1) + t1 * gl.th;

	corner_verts[1].position[0] = x + w;
	corner_verts[1].position[1] = y;
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texcoord[0] = gl.sl * (1 - s2) + s2 * gl.sh;
	corner_verts[1].texcoord[1] = gl.tl * (1 - t1) + t1 * gl.th;

	corner_verts[2].position[0] = x + w;
	corner_verts[2].position[1] = y + h;
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texcoord[0] = gl.sl * (1 - s2) + s2 * gl.sh;
	corner_verts[2].texcoord[1] = gl.tl * (1 - t2) + t2 * gl.th;

	corner_verts[3].position[0] = x;
	corner_verts[3].position[1] = y + h;
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texcoord[0] = gl.sl * (1 - s1) + s1 * gl.sh;
	corner_verts[3].texcoord[1] = gl.tl * (1 - t2) + t2 * gl.th;

	for (i = 0; i < 4; ++i)
	{
		corner_verts[i].color[0] = rgba[0];
		corner_verts[i].color[1] = rgba[1];
		corner_verts[i].color[2] = rgba[2];
		corner_verts[i].color[3] = rgba[3];
		corner_verts[i].texture_region[0] = corner_verts[0].texcoord[0];
		corner_verts[i].texture_region[1] = corner_verts[0].texcoord[1];
		corner_verts[i].texture_region[2] = corner_verts[2].texcoord[0];
		corner_verts[i].texture_region[3] = corner_verts[2].texcoord[1];
	}

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vkCmdBindVertexBuffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	Draw_BindPicState (cbx, gl.gltexture, alpha_blend, force_linear ? DRAW_FILTER_LINEAR : Draw_GetPicFilter ());
	vkCmdDraw (cbx->cb, 6, 1, 0, 0);
}

void Draw_SubPic (cb_context_t *cbx, float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2, float *rgb, float alpha)
{
	Draw_SubPicInternal (cbx, x, y, w, h, pic, s1, t1, s2, t2, rgb, alpha, false, false);
}

void Draw_SubPicLinear (cb_context_t *cbx, float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2, float *rgb, float alpha)
{
	Draw_SubPicInternal (cbx, x, y, w, h, pic, s1, t1, s2, t2, rgb, alpha, true, false);
}

void Draw_SubPicLinearBlend (
	cb_context_t *cbx, float x, float y, float w, float h, qpic_t *pic, float s1, float t1, float s2, float t2, float *rgb, float alpha)
{
	Draw_SubPicInternal (cbx, x, y, w, h, pic, s1, t1, s2, t2, rgb, alpha, true, true);
}

/*
=============
Draw_TransPicTranslate -- johnfitz -- rewritten to use texmgr to do translation

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (cb_context_t *cbx, float x, float y, qpic_t *pic, int top, int bottom)
{
	static int oldtop = -2;
	static int oldbottom = -2;

	if (top != oldtop || bottom != oldbottom)
	{
		glpic_t p;
		memcpy (&p, pic->data, sizeof (glpic_t));
		gltexture_t *glt = p.gltexture;
		oldtop = top;
		oldbottom = bottom;
		TexMgr_ReloadImage (glt, top, bottom);
	}
	Draw_Pic (cbx, x, y, pic, 1.0f, false);
}

/*
================
Draw_ConsoleBackground -- johnfitz -- rewritten
================
*/
void Draw_ConsoleBackground (cb_context_t *cbx)
{
	qpic_t *pic;
	float	alpha;

	pic = Draw_CachePic ("gfx/conback.lmp");
	pic->width = vid.conwidth;
	pic->height = vid.conheight;

	alpha = (con_forcedup) ? 1.0 : scr_conalpha.value;

	GL_SetCanvas (cbx, CANVAS_CONSOLE); // in case this is called from weird places

	if (alpha > 0.0)
	{
		Draw_Pic (cbx, 0, 0, pic, alpha, alpha < 1.0f);
	}
}

/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (cb_context_t *cbx, float x, float y, float w, float h)
{
	glpic_t gl;
	memcpy (&gl, draw_backtile->data, sizeof (glpic_t));

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset (&corner_verts, 255, sizeof (corner_verts));

	corner_verts[0].position[0] = x;
	corner_verts[0].position[1] = y;
	corner_verts[0].position[2] = 0.0f;
	corner_verts[0].texcoord[0] = x / 64.0;
	corner_verts[0].texcoord[1] = y / 64.0;

	corner_verts[1].position[0] = x + w;
	corner_verts[1].position[1] = y;
	corner_verts[1].position[2] = 0.0f;
	corner_verts[1].texcoord[0] = (x + w) / 64.0;
	corner_verts[1].texcoord[1] = y / 64.0;

	corner_verts[2].position[0] = x + w;
	corner_verts[2].position[1] = y + h;
	corner_verts[2].position[2] = 0.0f;
	corner_verts[2].texcoord[0] = (x + w) / 64.0;
	corner_verts[2].texcoord[1] = (y + h) / 64.0;

	corner_verts[3].position[0] = x;
	corner_verts[3].position[1] = y + h;
	corner_verts[3].position[2] = 0.0f;
	corner_verts[3].texcoord[0] = x / 64.0;
	corner_verts[3].texcoord[1] = (y + h) / 64.0;

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_BLEND);
	vkCmdBindDescriptorSets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cbx->current_pipeline.layout.handle, 0, 1, &gl.gltexture->descriptor_set, 0, NULL);
	vkCmdBindVertexBuffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	vkCmdDraw (cbx->cb, 6, 1, 0, 0);
}

/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (cb_context_t *cbx, float x, float y, float w, float h, int c, float alpha) // johnfitz -- added alpha
{
	int	  i;
	byte *pal = (byte *)d_8to24table; // johnfitz -- use d_8to24table instead of host_basepal

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset (&corner_verts, 0, sizeof (corner_verts));

	corner_verts[0].position[0] = x;
	corner_verts[0].position[1] = y;

	corner_verts[1].position[0] = x + w;
	corner_verts[1].position[1] = y;

	corner_verts[2].position[0] = x + w;
	corner_verts[2].position[1] = y + h;

	corner_verts[3].position[0] = x;
	corner_verts[3].position[1] = y + h;

	for (i = 0; i < 4; ++i)
	{
		corner_verts[i].color[0] = pal[c * 4];
		corner_verts[i].color[1] = pal[c * 4 + 1];
		corner_verts[i].color[2] = pal[c * 4 + 2];
		corner_verts[i].color[3] = alpha * 255;
	}

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vkCmdBindVertexBuffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_NOTEX_BLEND);
	vkCmdDraw (cbx->cb, 6, 1, 0, 0);
}

/*
================
Draw_FadeScreen
================
*/
void Draw_FadeScreen (cb_context_t *cbx)
{
	int i;

	GL_SetCanvas (cbx, CANVAS_DEFAULT);

	VkBuffer	   buffer;
	VkDeviceSize   buffer_offset;
	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (6 * sizeof (basicvertex_t), &buffer, &buffer_offset);

	basicvertex_t corner_verts[4];
	memset (&corner_verts, 0, sizeof (corner_verts));

	corner_verts[0].position[0] = 0.0f;
	corner_verts[0].position[1] = 0.0f;

	corner_verts[1].position[0] = glwidth;
	corner_verts[1].position[1] = 0.0f;

	corner_verts[2].position[0] = glwidth;
	corner_verts[2].position[1] = glheight;

	corner_verts[3].position[0] = 0.0f;
	corner_verts[3].position[1] = glheight;

	for (i = 0; i < 4; ++i)
		corner_verts[i].color[3] = 128;

	vertices[0] = corner_verts[0];
	vertices[1] = corner_verts[1];
	vertices[2] = corner_verts[2];
	vertices[3] = corner_verts[2];
	vertices[4] = corner_verts[3];
	vertices[5] = corner_verts[0];

	vkCmdBindVertexBuffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_NOTEX_BLEND);
	vkCmdDraw (cbx->cb, 6, 1, 0, 0);
}

/*
================
GL_OrthoMatrix
================
*/
static void GL_UpdateUIPanelSourceClip (cb_context_t *cbx);

static void GL_OrthoMatrix (cb_context_t *cbx, float left, float right, float bottom, float top, float n, float f)
{
	float tx = -(right + left) / (right - left);
	float ty = (top + bottom) / (top - bottom);
	float tz = -(f + n) / (f - n);

	float matrix[16];
	memset (&matrix, 0, sizeof (matrix));

	// First column
	matrix[0 * 4 + 0] = 2.0f / (right - left);

	// Second column
	matrix[1 * 4 + 1] = -2.0f / (top - bottom);

	// Third column
	matrix[2 * 4 + 2] = -2.0f / (f - n);

	// Fourth column
	matrix[3 * 4 + 0] = tx;
	matrix[3 * 4 + 1] = ty;
	matrix[3 * 4 + 2] = tz;
	matrix[3 * 4 + 3] = 1.0f;

	memcpy (cbx->canvas_ortho_matrix, matrix, sizeof (matrix));
	cbx->canvas_ortho_base_clip_rect[0] = q_min (left, right);
	cbx->canvas_ortho_base_clip_rect[1] = q_min (bottom, top);
	cbx->canvas_ortho_base_clip_rect[2] = q_max (left, right);
	cbx->canvas_ortho_base_clip_rect[3] = q_max (bottom, top);
	GL_UpdateUIPanelSourceClip (cbx);

	if (!cbx->ui_panel_active)
		R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof (matrix), matrix);
}

static void GL_SetUIPanelEmptyClip (float rect[4])
{
	// Keep one axis inverted so the shader's inclusive bounds test rejects
	// every finite source coordinate, including a zero-area requested clip.
	rect[0] = 1.0f;
	rect[1] = 0.0f;
	rect[2] = 0.0f;
	rect[3] = 1.0f;
}

static void GL_UpdateUIPanelSourceClip (cb_context_t *cbx)
{
	memcpy (cbx->canvas_ortho_clip_rect, cbx->canvas_ortho_base_clip_rect, sizeof (cbx->canvas_ortho_clip_rect));
	if (!cbx->ui_panel_active || !cbx->ui_panel_source_clip_active)
		return;

	if (cbx->ui_panel_source_clip_empty)
	{
		GL_SetUIPanelEmptyClip (cbx->canvas_ortho_clip_rect);
		return;
	}

	const double left = q_max ((double)cbx->canvas_ortho_base_clip_rect[0], cbx->ui_panel_source_clip_rect[0]);
	const double top = q_max ((double)cbx->canvas_ortho_base_clip_rect[1], cbx->ui_panel_source_clip_rect[1]);
	const double right = q_min ((double)cbx->canvas_ortho_base_clip_rect[2], cbx->ui_panel_source_clip_rect[2]);
	const double bottom = q_min ((double)cbx->canvas_ortho_base_clip_rect[3], cbx->ui_panel_source_clip_rect[3]);
	if (!(right > left) || !(bottom > top))
	{
		GL_SetUIPanelEmptyClip (cbx->canvas_ortho_clip_rect);
		return;
	}

	cbx->canvas_ortho_clip_rect[0] = (float)left;
	cbx->canvas_ortho_clip_rect[1] = (float)top;
	cbx->canvas_ortho_clip_rect[2] = (float)right;
	cbx->canvas_ortho_clip_rect[3] = (float)bottom;
}

void GL_SetUIPanelSourceClip (cb_context_t *cbx, float x, float y, float width, float height)
{
	if (!cbx || !cbx->ui_panel_active)
		return;

	cbx->ui_panel_source_clip_active = true;
	cbx->ui_panel_source_clip_empty = !isfinite (x) || !isfinite (y) || !isfinite (width) || !isfinite (height) || width <= 0.0f || height <= 0.0f;
	if (cbx->ui_panel_source_clip_empty)
	{
		memset (cbx->ui_panel_source_clip_rect, 0, sizeof (cbx->ui_panel_source_clip_rect));
	}
	else
	{
		cbx->ui_panel_source_clip_rect[0] = (double)x;
		cbx->ui_panel_source_clip_rect[1] = (double)y;
		cbx->ui_panel_source_clip_rect[2] = (double)x + (double)width;
		cbx->ui_panel_source_clip_rect[3] = (double)y + (double)height;
	}
	GL_UpdateUIPanelSourceClip (cbx);
}

void GL_ClearUIPanelSourceClip (cb_context_t *cbx)
{
	if (!cbx)
		return;
	cbx->ui_panel_source_clip_active = false;
	cbx->ui_panel_source_clip_empty = false;
	memset (cbx->ui_panel_source_clip_rect, 0, sizeof (cbx->ui_panel_source_clip_rect));
	GL_UpdateUIPanelSourceClip (cbx);
}

static void GL_SetUIPanelFullScissor (cb_context_t *cbx)
{
	const VkRect2D full_scissor = {{0, 0}, {(uint32_t)vid.width, (uint32_t)vid.height}};
	vkCmdSetScissor (cbx->cb, 0, 1, &full_scissor);
}

/*
================
GL_Viewport
================
*/
void GL_Viewport (cb_context_t *cbx, float x, float y, float width, float height, float min_depth, float max_depth)
{
	VkViewport viewport;
	viewport.x = x;
	viewport.y = vid.height - (y + height);
	viewport.width = width;
	viewport.height = height;
	viewport.minDepth = min_depth;
	viewport.maxDepth = max_depth;

	cbx->canvas_viewport = viewport;
	vkCmdSetViewport (cbx->cb, 0, 1, &viewport);
}

static void GL_SetUIPanelCanvasTransform (cb_context_t *cbx)
{
	float viewport_matrix[16];
	memset (viewport_matrix, 0, sizeof (viewport_matrix));

	const float sx = cbx->canvas_viewport.width / (float)vid.width;
	const float tx = 2.0f * cbx->canvas_viewport.x / (float)vid.width + sx - 1.0f;
	const float sy = cbx->canvas_viewport.height / (float)vid.height;
	const float ty = 2.0f * cbx->canvas_viewport.y / (float)vid.height + sy - 1.0f;
	viewport_matrix[0] = sx;
	viewport_matrix[5] = sy;
	viewport_matrix[10] = 1.0f;
	viewport_matrix[12] = tx;
	viewport_matrix[13] = ty;
	viewport_matrix[15] = 1.0f;

	memcpy (cbx->ui_panel_mvp, vulkan_globals.view_projection_matrix, sizeof (cbx->ui_panel_mvp));
	MatrixMultiply (cbx->ui_panel_mvp, cbx->ui_panel_world_from_ndc);
	MatrixMultiply (cbx->ui_panel_mvp, viewport_matrix);
	MatrixMultiply (cbx->ui_panel_mvp, cbx->canvas_ortho_matrix);
	cbx->ui_panel_mvp_valid = true;

	// A layout switch can clear these values, so R_BindGraphicsPipeline reapplies
	// them after binding each UI pipeline. Push now as well when a compatible UI
	// layout is already active.
	if (cbx->subpass_type == SUBPASS_UI && cbx->current_pipeline.handle != VK_NULL_HANDLE &&
		cbx->current_pipeline.layout.push_constant_range.size >= UI_PANEL_FLAG_PUSH_CONSTANT_OFFSET + sizeof (float))
	{
		R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof (cbx->ui_panel_mvp), cbx->ui_panel_mvp);
		R_PushConstants (
			cbx, VK_SHADER_STAGE_ALL_GRAPHICS, UI_PANEL_CLIP_PUSH_CONSTANT_OFFSET, sizeof (cbx->canvas_ortho_clip_rect), cbx->canvas_ortho_clip_rect);
		const float enabled = 1.0f;
		R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, UI_PANEL_FLAG_PUSH_CONSTANT_OFFSET, sizeof (enabled), &enabled);
	}

	const VkViewport full_viewport = {0.0f, 0.0f, (float)vid.width, (float)vid.height, 0.0f, 1.0f};
	vkCmdSetViewport (cbx->cb, 0, 1, &full_viewport);
}

void GL_BeginUIPanel (cb_context_t *cbx, const float world_from_ndc[16])
{
	memcpy (cbx->ui_panel_world_from_ndc, world_from_ndc, sizeof (cbx->ui_panel_world_from_ndc));
	cbx->ui_panel_active = true;
	cbx->ui_panel_modern_hud = false;
	cbx->ui_panel_classic_hud = false;
	cbx->ui_panel_mvp_valid = false;
	GL_ClearUIPanelSourceClip (cbx);
	cbx->current_canvas = CANVAS_INVALID;
	GL_SetUIPanelFullScissor (cbx);
	// The post-upscale or prior UI pipeline may still be bound; defer panel pushes until a compatible UI pipeline is active.
}

void GL_EndUIPanel (cb_context_t *cbx)
{
	GL_ClearUIPanelSourceClip (cbx);
	cbx->ui_panel_active = false;
	cbx->ui_panel_modern_hud = false;
	cbx->ui_panel_classic_hud = false;
	cbx->ui_panel_mvp_valid = false;
	cbx->current_canvas = CANVAS_INVALID;
	GL_SetUIPanelFullScissor (cbx);

	// Use the bound layout (including the basic-layout upscaler) only when it covers the panel flag.
	if (cbx->subpass_type == SUBPASS_UI && cbx->current_pipeline.handle != VK_NULL_HANDLE &&
		cbx->current_pipeline.layout.push_constant_range.size >= UI_PANEL_FLAG_PUSH_CONSTANT_OFFSET + sizeof (float))
	{
		const float disabled = 0.0f;
		R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, UI_PANEL_FLAG_PUSH_CONSTANT_OFFSET, sizeof (disabled), &disabled);
	}

	const VkViewport full_viewport = {0.0f, 0.0f, (float)vid.width, (float)vid.height, 0.0f, 1.0f};
	vkCmdSetViewport (cbx->cb, 0, 1, &full_viewport);
}

static qboolean GL_SetModernHUDCanvas (cb_context_t *cbx, canvastype canvas)
{
	float fitting_scale;
	float origin_x, origin_y, canvas_x, canvas_y;
	float canvas_width = 320.0f;
	float canvas_height = 200.0f;

	if (!cbx->ui_panel_modern_hud)
		return false;

	fitting_scale = q_min ((float)glwidth / 640.0f, (float)glheight / 400.0f);
	if (!isfinite (fitting_scale) || fitting_scale <= 0.0f)
		return false;
	origin_x = (glwidth - 640.0f * fitting_scale) * 0.5f;
	origin_y = (glheight - 400.0f * fitting_scale) * 0.5f;

	switch (canvas)
	{
	case CANVAS_BOTTOMLEFT:
		canvas_x = 0.0f;
		canvas_y = 200.0f;
		break;
	case CANVAS_TOPLEFT:
		canvas_x = 0.0f;
		canvas_y = 0.0f;
		break;
	case CANVAS_BOTTOMRIGHT:
		canvas_x = 320.0f;
		canvas_y = 200.0f;
		break;
	case CANVAS_SBAR:
		/* Modern score/death is a centered 320x48 strip at the panel base. */
		canvas_x = 160.0f;
		canvas_y = 352.0f;
		canvas_height = 48.0f;
		break;
	case CANVAS_MENU:
		/* Keep long player names inside the score canvas. The first 320
		 * units retain their position relative to the status bar. */
		canvas_x = 160.0f;
		canvas_y = 200.0f;
		canvas_width = 416.0f;
		break;
	case CANVAS_TOPRIGHT:
		canvas_x = 320.0f;
		canvas_y = 0.0f;
		break;
	default:
		return false;
	}

	GL_OrthoMatrix (cbx, 0, canvas_width, canvas_height, 0, -99999, 99999);
	GL_Viewport (cbx, origin_x + canvas_x * fitting_scale,
		glheight - (origin_y + (canvas_y + canvas_height) * fitting_scale),
		canvas_width * fitting_scale, canvas_height * fitting_scale, 0.0f, 1.0f);
	return true;
}

/*
================
GL_SetCanvas -- johnfitz -- support various canvas types
================
*/
void GL_SetCanvas (cb_context_t *cbx, canvastype newcanvas)
{
	if (newcanvas == cbx->current_canvas)
		return;
	if (cbx->ui_panel_active && cbx->current_canvas == CANVAS_CSQC && newcanvas != CANVAS_CSQC)
		GL_ClearUIPanelSourceClip (cbx);

	extern vrect_t scr_vrect;
	float		   s, u, v;
	int			   lines;

	cbx->current_canvas = newcanvas;

	switch (newcanvas)
	{
	case CANVAS_NONE:
		break;
	case CANVAS_DEFAULT:
		GL_OrthoMatrix (cbx, 0, glwidth, glheight, 0, -99999, 99999);
		GL_Viewport (cbx, 0, 0, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_CONSOLE:
		lines = vid.conheight - (scr_con_current * vid.conheight / glheight);
		GL_OrthoMatrix (cbx, 0, vid.conwidth, vid.conheight + lines, lines, -99999, 99999);
		GL_Viewport (cbx, 0, 0, glwidth, glheight, 0.0f, 1.0f);
		break;
	case CANVAS_MENU:
		if (!GL_SetModernHUDCanvas (cbx, newcanvas))
		{
			if (cbx->ui_panel_classic_hud)
			{
				/* The 200-high score surface ends at the classic bar's base. */
				s = CLAMP (1.0f, scr_sbarscale.value, (float)glwidth / 320.0f);
				GL_OrthoMatrix (cbx, 0, 416, 200, 0, -99999, 99999);
				GL_Viewport (cbx, (glwidth - 320.0f * s) * 0.5f,
					0, 416.0f * s, 200.0f * s, 0.0f, 1.0f);
			}
			else
			{
				s = M_MenuCanvasScale ();
				u = (glwidth - (320.0f * s)) / (2.0f * s);
				v = (glheight - (200.0f * s)) / (2.0f * s);
				GL_OrthoMatrix (cbx, -u, 320.0f + u, 200.0f + v, -v, -99999, 99999);
				GL_Viewport (cbx, 0, 0, glwidth, glheight, 0.0f, 1.0f);
			}
		}
		break;
	case CANVAS_CSQC:
	{
		csqc_display_t display = SCR_GetCSQCDisplay ();
		GL_OrthoMatrix (cbx, 0, glwidth / display.pixel_scale[0], glheight / display.pixel_scale[1], 0, -99999, 99999);
		GL_Viewport (cbx, 0, 0, glwidth, glheight, 0.0f, 1.0f);
		break;
	}
	case CANVAS_SBAR:
		if (!GL_SetModernHUDCanvas (cbx, newcanvas))
		{
			s = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
			if (cl.gametype == GAME_DEATHMATCH && scr_style.value < 2.0f && !cbx->ui_panel_classic_hud)
			{
				GL_OrthoMatrix (cbx, 0, glwidth / s, 48, 0, -99999, 99999);
				GL_Viewport (cbx, 0, 0, glwidth, 48 * s, 0.0f, 1.0f);
			}
			else
			{
				GL_OrthoMatrix (cbx, 0, 320, 48, 0, -99999, 99999);
				GL_Viewport (cbx, (glwidth - 320 * s) / 2, 0, 320 * s, 48 * s, 0.0f, 1.0f);
			}
		}
		break;
	case CANVAS_WARPIMAGE:
		GL_OrthoMatrix (cbx, 0, 128, 0, 128, -99999, 99999);
		GL_Viewport (cbx, 0, glheight - WARPIMAGESIZE, WARPIMAGESIZE, WARPIMAGESIZE, 0.0f, 1.0f);
		break;
	case CANVAS_CROSSHAIR: // 0,0 is center of viewport
		s = CLAMP (1.0, scr_crosshairscale.value, 10.0);
		GL_OrthoMatrix (cbx, scr_vrect.width / -2 / s, scr_vrect.width / 2 / s, scr_vrect.height / 2 / s, scr_vrect.height / -2 / s, -99999, 99999);
		GL_Viewport (cbx, scr_vrect.x, glheight - scr_vrect.y - scr_vrect.height, scr_vrect.width & ~1, scr_vrect.height & ~1, 0.0f, 1.0f);
		break;
	case CANVAS_BOTTOMLEFT:				   // used by devstats
		if (!GL_SetModernHUDCanvas (cbx, newcanvas))
		{
			s = (float)glwidth / vid.conwidth; // use console scale
			GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
			GL_Viewport (cbx, 0, 0, 320 * s, 200 * s, 0.0f, 1.0f);
		}
		break;
	case CANVAS_TOPLEFT:				   // for modern HUD frag counter
		if (!GL_SetModernHUDCanvas (cbx, newcanvas))
		{
			s = (float)glwidth / vid.conwidth; // use console scale
			GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
			GL_Viewport (cbx, 0, glheight - 200 * s, 320 * s, 200 * s, 0.0f, 1.0f);
		}
		break;
	case CANVAS_BOTTOMRIGHT:			   // used by fps/clock
		if (!GL_SetModernHUDCanvas (cbx, newcanvas))
		{
			s = (float)glwidth / vid.conwidth; // use console scale
			GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
			GL_Viewport (cbx, glwidth - 320 * s, 0, 320 * s, 200 * s, 0.0f, 1.0f);
		}
		break;
	case CANVAS_TOPRIGHT:				   // for modern HUD weapon icons
		if (!GL_SetModernHUDCanvas (cbx, newcanvas))
		{
			s = (float)glwidth / vid.conwidth; // use console scale
			GL_OrthoMatrix (cbx, 0, 320, 200, 0, -99999, 99999);
			GL_Viewport (cbx, glwidth - 320 * s, glheight - 200 * s, 320 * s, 200 * s, 0.0f, 1.0f);
		}
		break;
	default:
		Sys_Error ("GL_SetCanvas: bad canvas type");
	}

	if (cbx->ui_panel_active && newcanvas != CANVAS_NONE)
		GL_SetUIPanelCanvasTransform (cbx);
	else if (newcanvas == CANVAS_NONE)
		cbx->ui_panel_mvp_valid = false;
}

//==============================================================================
//
//  3D BILLBOARD DRAWING
//
//==============================================================================

/*
================
Draw_FillCharacterQuad_3D
================
*/
static void Draw_FillCharacterQuad_3D (
	const vec3_t origin, const vec3_t right, const vec3_t up, float xoff, float yoff, float size, char num, const byte color[4], basicvertex_t *output)
{
	int	  row, col;
	float frow, fcol, tile_size;

	xoff *= size;
	yoff *= size;

	row = (unsigned char)num >> 4;
	col = (unsigned char)num & 15;

	frow = row * 0.0625;
	fcol = col * 0.0625;
	tile_size = 0.0625;

	basicvertex_t corner_verts[4];
	memset (&corner_verts, 255, sizeof (corner_verts));

	VectorMA (origin, size / 2 - yoff, up, &corner_verts[0].position[0]);
	VectorMA (&corner_verts[0].position[0], -size / 2 + xoff, right, &corner_verts[0].position[0]);
	corner_verts[0].texcoord[0] = fcol;
	corner_verts[0].texcoord[1] = frow;
	memcpy (corner_verts[0].color, color, sizeof (corner_verts[0].color));

	VectorMA (&corner_verts[0].position[0], size, right, &corner_verts[1].position[0]);
	corner_verts[1].texcoord[0] = fcol + tile_size;
	corner_verts[1].texcoord[1] = frow;
	memcpy (corner_verts[1].color, color, sizeof (corner_verts[1].color));

	VectorMA (&corner_verts[1].position[0], -size, up, &corner_verts[2].position[0]);
	corner_verts[2].texcoord[0] = fcol + tile_size;
	corner_verts[2].texcoord[1] = frow + tile_size;
	memcpy (corner_verts[2].color, color, sizeof (corner_verts[2].color));

	VectorMA (&corner_verts[2].position[0], -size, right, &corner_verts[3].position[0]);
	corner_verts[3].texcoord[0] = fcol;
	corner_verts[3].texcoord[1] = frow + tile_size;
	memcpy (corner_verts[3].color, color, sizeof (corner_verts[3].color));

	output[0] = corner_verts[0];
	output[1] = corner_verts[1];
	output[2] = corner_verts[2];
	output[3] = corner_verts[2];
	output[4] = corner_verts[3];
	output[5] = corner_verts[0];
}

static int Draw_BuildStringVertices_3D (
	const vec3_t origin, const vec3_t right, const vec3_t up, float size, const char *str, const byte color[4], VkBuffer *buffer,
	VkDeviceSize *buffer_offset)
{
	int num_verts = 0;
	for (const char *tmp = str; *tmp != 0; ++tmp)
		if (*tmp != ' ')
			num_verts += 6;

	if (num_verts == 0)
		return 0;

	basicvertex_t *vertices = (basicvertex_t *)R_VertexAllocate (num_verts * sizeof (basicvertex_t), buffer, buffer_offset);
	float xoff = -0.5f * strlen (str) + 0.5f;
	int vertex_offset = 0;
	for (; *str != 0; ++str)
	{
		if (*str != ' ')
		{
			Draw_FillCharacterQuad_3D (origin, right, up, xoff, 0, size, *str, color, vertices + vertex_offset);
			vertex_offset += 6;
		}
		xoff += 1.0f;
	}

	return num_verts;
}

/*
================
Draw_String_3D
================
*/
void Draw_String_3D (cb_context_t *cbx, vec3_t coords, float size, const char *str)
{
	static const byte white[4] = {255, 255, 255, 255};
	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	const int num_verts = str ? Draw_BuildStringVertices_3D (coords, vright, vup, size, str, white, &buffer, &buffer_offset) : 0;
	if (num_verts == 0)
		return;

	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_ALPHATEST);
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cbx->current_pipeline.layout.handle, 0, 1, &char_texture->descriptor_set, 0, NULL);
	vulkan_globals.vk_cmd_draw (cbx->cb, num_verts, 1, 0, 0);
}

/* The co-op name tag reuses world glyph geometry and scene projection. Its
 * late-particle pipeline tests depth without writing it, as in OpenVR. */
void Draw_String_3DColor (cb_context_t *cbx, const vec3_t origin, const vec3_t right, const vec3_t up,
	float size, const char *str, const vec3_t color, float alpha)
{
	if (!str || !*str || !char_texture)
		return;
	const byte vertex_color[4] = {
		(byte)(CLAMP (0.0f, color[0], 1.0f) * 255.0f),
		(byte)(CLAMP (0.0f, color[1], 1.0f) * 255.0f),
		(byte)(CLAMP (0.0f, color[2], 1.0f) * 255.0f),
		(byte)(CLAMP (0.0f, alpha, 1.0f) * 255.0f),
	};
	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	const int num_verts = Draw_BuildStringVertices_3D (origin, right, up, size, str,
		vertex_color, &buffer, &buffer_offset);
	if (num_verts == 0)
		return;

	R_BindGraphicsPipeline (cbx, PIPELINE_COOP_NAMETAG);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0,
		sizeof (vulkan_globals.view_projection_matrix), vulkan_globals.view_projection_matrix);
	Fog_DisableGFog (cbx);
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	vulkan_globals.vk_cmd_bind_descriptor_sets (cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		cbx->current_pipeline.layout.handle, 0, 1, &char_texture->descriptor_set, 0, NULL);
	vulkan_globals.vk_cmd_draw (cbx->cb, num_verts, 1, 0, 0);
}

void Draw_String_3DDepth (
	cb_context_t *cbx, const vec3_t origin, const vec3_t right, const vec3_t up, float size, const char *str, const vec3_t color)
{
	if (!str || !*str)
		return;

	const byte vertex_color[4] = {
		(byte)(CLAMP (0.0f, color[0], 1.0f) * 255.0f),
		(byte)(CLAMP (0.0f, color[1], 1.0f) * 255.0f),
		(byte)(CLAMP (0.0f, color[2], 1.0f) * 255.0f),
		255,
	};
	VkBuffer buffer;
	VkDeviceSize buffer_offset;
	const int num_verts = Draw_BuildStringVertices_3D (origin, right, up, size, str, vertex_color, &buffer, &buffer_offset);
	if (num_verts == 0)
		return;

	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_ALPHATEST_DEPTH);
	R_PushConstants (cbx, VK_SHADER_STAGE_ALL_GRAPHICS, 0, sizeof (vulkan_globals.view_projection_matrix), vulkan_globals.view_projection_matrix);
	Fog_DisableGFog (cbx);
	vulkan_globals.vk_cmd_bind_vertex_buffers (cbx->cb, 0, 1, &buffer, &buffer_offset);
	vulkan_globals.vk_cmd_bind_descriptor_sets (
		cbx->cb, VK_PIPELINE_BIND_POINT_GRAPHICS, cbx->current_pipeline.layout.handle, 0, 1, &char_texture->descriptor_set, 0, NULL);
	vulkan_globals.vk_cmd_draw (cbx->cb, num_verts, 1, 0, 0);
}
