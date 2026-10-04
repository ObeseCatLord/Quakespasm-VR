/* GPL-2.0-or-later. Private Steam Audio room simulation/DSP boundary. */
#ifndef SND_ROOM_H
#define SND_ROOM_H
#include "snd_steamaudio.h"
#include <phonon.h>
sa_room_t *SAR_Create(IPLContext context, IPLHRTF hrtf, sa_geometry_t *geometry);
void SAR_Destroy(sa_room_t *room); /* room detached first; joins worker and may block */
void SAR_Update(sa_room_t *room, const sa_listener_t *listener, const sa_settings_t *settings);
void SAR_Reset(sa_room_t *room, int voice_only); /* callback excluded, or callback itself */
/* Callback-local fault flags, including overflow of finite wet additions.
 * The caller owns fault statistics; SAR_Stats is protected by the worker lock. */
enum {
    SAR_NONFINITE_SFX = 1u << 0,
    SAR_NONFINITE_VOICE = 1u << 1,
    SAR_NONFINITE_DECODE = 1u << 2,
    SAR_NONFINITE_HANDOFF = 1u << 3
};
unsigned SAR_Render(sa_room_t *room, const float *sfx, const float *voice, float *stereo,
    const sa_listener_t *listener, const sa_settings_t *settings);
void SAR_Stats(sa_room_t *room, sa_room_stats_t *stats);
#endif
