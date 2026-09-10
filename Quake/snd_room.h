/* GPL-2.0-or-later. Private Steam Audio room simulation/DSP boundary. */
#ifndef SND_ROOM_H
#define SND_ROOM_H
#include "snd_steamaudio.h"
#include <phonon.h>
typedef struct sa_room_s sa_room_t;
sa_room_t *SAR_Create(IPLContext context, IPLHRTF hrtf, sa_geometry_t *geometry);
void SAR_Destroy(sa_room_t *room); /* callback excluded; joins worker */
void SAR_Update(sa_room_t *room, const sa_listener_t *listener, const sa_settings_t *settings);
void SAR_Reset(sa_room_t *room, int voice_only); /* callback excluded, or callback itself */
void SAR_Render(sa_room_t *room, const float *sfx, const float *voice, float *stereo,
    const sa_listener_t *listener, const sa_settings_t *settings);
void SAR_Stats(sa_room_t *room, sa_room_stats_t *stats);
#endif
