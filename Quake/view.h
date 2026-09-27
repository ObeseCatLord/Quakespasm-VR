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

#ifndef _QUAKE_VIEW_H
#define _QUAKE_VIEW_H

extern cvar_t vid_gamma;
extern cvar_t vid_contrast;
extern cvar_t vr_eye_tracking;
extern cvar_t vr_foveation;
extern cvar_t vr_gunmodelpitch;
extern cvar_t vr_gunmodelscale;
extern cvar_t vr_gunmodely;

extern uint8_t v_blend[4];

void V_Init (void);
// Existing view/input owner; runtime lifetime remains in the backend.
qboolean V_UseTrackedView (void);
qboolean V_TrackedSessionActive (void);
float V_VRUnitsPerMetre (void);
float V_VRFloorOffset (void);
qboolean V_TrackedPlayerBase (float *viewheight);
void V_UpdateTrackedAim (void);
void V_ResetTrackedAim (void);
void V_RebaseTrackedAim (void);
void V_SetTrackedAngles (const vec3_t angles);
void V_PushTrackedYaw (void);
void V_RequestTrackedServerYaw (float yaw);
void V_ValidateTrackedServerYaw (void);
const float *V_TrackedViewAngles (void);
void V_TrackedAngleDelta (const vec3_t delta);
qboolean V_TrackedMovementAngles (int mode, int physical_offhand, vec3_t angles);
qboolean V_TrackedMappingYaw (float *yaw);
qboolean V_TrackedPresentationYaw (float *yaw);
qboolean V_TrackedHandBodyOffset (int physical_hand, vec3_t out);
qboolean V_TrackedPresentationHandAngles (int physical_hand, vec3_t angles);
qboolean V_TrackedPresentationHandBodyOffset (int physical_hand, vec3_t out);
/* Collision-free grip in the same presentation space as cl.viewent, plus
 * the live hand angles used to orient the viewmodel. */
qboolean V_TrackedPresentationHandWorldPose (int physical_hand,
	vec3_t origin, vec3_t hand_angles);
qboolean V_TrackedBodyOwnsRoomscale (void);
qboolean V_TrackedViewmodelActive (void);
qboolean V_TrackedViewmodelShouldHide (void);
qboolean V_AkimboPairReady (void);
void V_AkimboPairCollisionOffset (int physical_hand, vec3_t out_render_delta);
qboolean V_AkimboRecipeSupported (const char *source_model);
qboolean V_AkimboRecipeUsesPairedCollision (const char *source_model);
qboolean V_AkimboModelAngles (const char *source_model, int physical_hand,
	const vec3_t raw_hand_angles, vec3_t out);
qboolean V_AkimboTransformAnchor (int physical_hand,
	const vec3_t model_angles, vec3_t out_local);
qboolean V_AkimboDwellEdgeOffsets (int physical_hand,
	const vec3_t model_angles, vec3_t out_base, vec3_t out_tip);
/* Prepared single-held-mesh edges before retraction and the shared render delta.
 * Failure to prepare the rendered mesh also rejects physical contacts. */
qboolean V_HeldMeleeEdgeOffsets (vec3_t out_base, vec3_t out_tip,
	vec3_t out_collision);
qboolean V_HeldMeleeRawEdgeOffsets (const vec3_t hand_angles,
	vec3_t out_base, vec3_t out_tip);
entity_t *V_HeldMeleeEntity (void);
qboolean V_HeldMeleeRenderEntity (const entity_t *e);
int V_AkimboViewmodelHand (const entity_t *e);
entity_t *V_AkimboPairEntity (int physical_hand);
void V_ClearAkimboPair (void);
void V_PrepareAkimboPair (void);
void V_PrepareWeaponCollisionPresentation (void);
void V_ClearWeaponCollisionPresentation (void);
qboolean V_TrackedWeaponCollisionPresentation (vec3_t origin, vec3_t offset);
qboolean V_TurnTrackedYaw (float delta);
qboolean V_ApplyTrackedView (vec3_t angles, float *tracking_yaw);
void V_ResetBlend (void);
void V_RenderView (
	qboolean use_tasks, task_handle_t begin_rendering_task, task_handle_t setup_frame_task, task_handle_t draw_done_task, task_handle_t draw_gui_task);
void  V_CalcBlend (void);
void  V_SetupFrame (void);
float V_CalcRoll (vec3_t angles, vec3_t velocity);
void  V_RestoreAngles (void);
// void V_UpdatePalette (void); //johnfitz

#endif /* _QUAKE_VIEW_H */
