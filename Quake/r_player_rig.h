/* Code-defined VR body rigs for MDL player models. */
#ifndef R_PLAYER_RIG_H
#define R_PLAYER_RIG_H

#include "quakedef.h"

/* Builds (once per model) a skinned avatar from a vertex-animated player MDL
 * whose geometry matches a vr_player_rigs.h row. The rerelease Ranger skeleton
 * supplies the joint hierarchy and bind orientations. Main thread only.
 * Returns the generated custom avatar id, or -1. */
int R_PlayerRigEnsure (qmodel_t *model, const md5_skeleton_view_t *ranger);
/* True once R_PlayerRigEnsure has examined this model (built or not). */
qboolean R_PlayerRigSeen (const qmodel_t *model);
/* Cached result of R_PlayerRigEnsure without building: id or -1. */
int R_PlayerRigLookup (const qmodel_t *model);
/* Ranger ordinal for a model frame of a generated rig. Models whose frame names
 * follow the stock player use the stock ordinal of that name, or -1 when the
 * name has none; all other models use their ordinals as is. */
int R_PlayerRigCanonicalFrame (const qmodel_t *model, int frame);
void R_PlayerRigInit (void);

#endif
