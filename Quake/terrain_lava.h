/*
 * Read-only view of the particle lava for the renderer (r_terrain_lava.c), beyond the basic one in
 * terrain.h: velocities, the sleeping flag, light weights and the steam events. Listen server only.
 * GPL-2.0-or-later, as the rest of the engine.
 */
#ifndef TERRAIN_LAVA_H
#define TERRAIN_LAVA_H

#include <stdint.h>

#define TERRAIN_LAVA_STEAM_RING 256 // keep in step with LAVA_STEAM_RING in sv_terrain_lava.c

typedef struct
{
	float pos[3], nrm[3], strength; // a puff of steam where hot lava meets ice; strength about 1 per puff
} terrain_lava_steam_event_t;

typedef struct
{
	int			 count;
	const float *pos[3], *vel[3], *heat;
	const uint8_t *asleep;
	float		 radius; // particle spacing scale
	int			 num_lights;
	const float (*lights)[4]; // x y z radius
	const float *light_weight; // heat summed over the particles the light stands for
	unsigned	 steam_seq;	   // events written so far; event k lives in steam[k % TERRAIN_LAVA_STEAM_RING]
	const terrain_lava_steam_event_t *steam;
	double sim_time;
} terrain_lava_view2_t;

const terrain_lava_view2_t *SV_Terrain_LavaView2 (void); // NULL with no server or terrain

void R_TerrainLava_Init (void); // registers r_terrain_lava_* and terrain_lava_style; called from SV_Terrain_LavaInit

#endif
