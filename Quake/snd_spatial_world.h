/* Static BSP geometry adapter for the optional Steam Audio room. */
#ifndef SND_SPATIAL_WORLD_H
#define SND_SPATIAL_WORLD_H

#ifdef USE_STEAMAUDIO
void SpatialWorld_NewMap (void);
void SpatialWorld_Clear (void);
#else
#define SpatialWorld_NewMap() ((void)0)
#define SpatialWorld_Clear() ((void)0)
#endif

#endif
