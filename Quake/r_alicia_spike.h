/* Opt-in, sample-specific rendering experiment. Not a general VRM importer. */
#ifndef R_ALICIA_SPIKE_H
#define R_ALICIA_SPIKE_H
#include "r_vrik.h"
extern cvar_t r_alicia_direct, r_alicia_preview, r_alicia_pose, r_alicia_props;
void R_AliciaSpikeInit(void);
qboolean R_AliciaSpikeDraw(qmodel_t *model, const r_vrik_skincache_t *pose,
 int slot, const float light[3], const float shade[3], float alpha);
#endif
