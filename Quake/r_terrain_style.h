/*
 * Look styles for the editable terrain (r_terrain_style). Shared by r_terrain.c (drawing),
 * r_terrain_tex.c (style table, procedural atlas, fog) and r_terrain_props.c (props, light strings).
 * GPL-2.0-or-later, as the rest of the engine.
 */
#ifndef R_TERRAIN_STYLE_H
#define R_TERRAIN_STYLE_H

#include "terrain.h"

#define TERRAIN_STYLE_COUNT 4

enum
{
	TERRAIN_PROPS_NONE,
	TERRAIN_PROPS_WORKSHOP, // trees, presents, lanterns, strings of lights
	TERRAIN_PROPS_GLACIER,	// cold glow lights in the chambers
	TERRAIN_PROPS_AGARTHA,	// plinths with relics, pots, statues, candles
};

typedef struct
{
	const char *name;
	float		ambient[3]; // ambient light colour, times r_terrain_ambient
	float		lamp[3];	// eye lamp colour
	float		shadow[3];	// what occluded corners fall to instead of black
	float		lamp_reach; // times r_terrain_lamp
	float		shine;		// glint where the ice faces the lamp
	float		snow_from;	// up-facing amount where snow starts settling
	float		strata_snow;
	float		fog[4]; // density, r, g, b; density 0 leaves the map's fog alone
	int			props;
} terrain_style_t;

// Vertex of the textured terrain pipeline; keep in sync with terrain_vertex_input_attribute_descriptions (gl_rmisc.c).
typedef struct
{
	float pos[3];
	byte  light[4];	 // rgb: half the light, a: snow cover
	byte  params[4]; // projection axis, material, flags, ambient occlusion
} terrain_gpuvert_t;

#define TERRAIN_GPU_FULLBRIGHT 1 // light is the final colour
#define TERRAIN_GPU_PLAIN	   2 // lit, untextured

extern cvar_t r_terrain_style, r_terrain_props;

void				   R_TerrainStyle_Init (void);
int					   R_TerrainStyle_Current (void); // 0 is the original untextured look
const terrain_style_t *R_TerrainStyle_Get (void);
void				   R_TerrainStyle_Update (const terrain_t *t, unsigned epoch); // main thread, per frame
qboolean			   R_TerrainStyle_Bind (cb_context_t *cbx);					   // pipeline, atlas, push constants
void				   R_TerrainProps_Rebuild (const terrain_t *t);
void				   R_TerrainProps_Draw (cb_context_t *cbx);

#endif
