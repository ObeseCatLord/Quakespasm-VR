/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016-2021 vkQuake developers

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

#ifndef QUAKE_VR_INPUT_H
#define QUAKE_VR_INPUT_H

#include "protocol.h"
#include "cvar.h"
#include "vr_openxr.h"
#include "vr_fbt.h"
#include "vr_fbt_profile.h"
#include "vrik_codec.h"

enum
{
	VR_INPUT_ROLE_LEFT,
	VR_INPUT_ROLE_RIGHT
};

void VR_InputInit (void);
extern cvar_t vr_fbt_enabled;

typedef enum
{
	VR_INPUT_FBT_CALIBRATION_IDLE,
	VR_INPUT_FBT_CALIBRATION_READY,
	VR_INPUT_FBT_CALIBRATION_CAPTURING,
	VR_INPUT_FBT_CALIBRATION_PREVIEW
} vr_input_fbt_calibration_state_t;

typedef struct
{
	vr_input_fbt_calibration_state_t state;
	char profile_name[VR_FBT_PROFILE_NAME_MAX];
	qboolean profile_valid;
	unsigned int required_role_mask;
	unsigned int accepted[VR_FBT_ROLE_COUNT];
	unsigned int rejected[VR_FBT_ROLE_COUNT];
	unsigned int snapshot_rejected;
	double elapsed_seconds;
} vr_input_fbt_calibration_status_t;

/* Main-thread menu access to the input-owned manager. Candidate cycling only
 * selects connected, unambiguous identities that are not assigned elsewhere. */
qboolean VR_InputFBTGetRoleStatus (vr_fbt_role_t role,
	vr_fbt_role_status_t *status);
qboolean VR_InputFBTCycleRole (vr_fbt_role_t role, int direction);
qboolean VR_InputFBTGetCalibrationStatus (
	vr_input_fbt_calibration_status_t *status);
/* Starts a menu calibration under the first unused menu_fbt_N profile name. */
qboolean VR_InputFBTBeginMenuCalibration (void);

void VR_InputCommands (const vrxr_frame_t *frame);
void VR_InputMenuPanelTrigger (const vrxr_frame_t *frame, qboolean panel_drawn);
void VR_InputMove (usercmd_t *pending);
void VR_InputApplyPending (usercmd_t *cmd);
void VR_InputCommitGorillaCommand (const usercmd_t *cmd);
int VR_InputDominantPhysicalHand (void);
/* The input owner's identity/profile/neutral gate for a physical XR hand. */
qboolean VR_InputPhysicalHandAccepted (const vrxr_frame_t *frame, int physical_hand);
extern cvar_t vr_weapon_collision;
qboolean VR_WeaponCollisionAuthorized (void);
/* Selected gesture-mode melee policy; independent of current hand tracking. */
qboolean VR_InputGestureMeleeActive (void);
/* Selected supported axe edge in the raw presentation hand frame. False without
 * an admitted MELEE profile; callers may use calibrated generic fallback. */
qboolean VR_InputStockAxePresentationEdgeOffsets (int physical_hand,
	vec3_t base, vec3_t tip);
struct entity_s;
struct aliashdr_s;
/* Read-only ready-pose selection for the selected gesture-mode VR viewmodel. */
qboolean VR_InputMeleeReadyPose (const struct entity_s *entity,
	const struct aliashdr_s *geometry, int *pose_out);
qboolean VR_InputCrosshairAimRay (vec3_t start, vec3_t forward);
int VR_InputCrosshairAimRays (vec3_t starts[2], vec3_t forwards[2]);
void VR_InputTriggerHaptic (int logical_role, float duration_seconds, float amplitude);
qboolean VR_InputSuppressUncalibratedAttack (const usercmd_t *cmd);
/* Preview observes intent; only command finalization consumes it. */
unsigned int VR_InputMergeMeleeAttack (unsigned int buttons, qboolean isfinal);
void VR_InputInvalidateMotion (void);
void VR_InputResetMotionContinuity (void);
void VR_InputClear (void);
qboolean VR_InputBuildVRIKPose (vrik_codec_pose_t *pose);

/* Read-only snapshot for the renderer's per-frame debug copy.
 * Positions are bounded metres in player-root axes; body_yaw_degrees maps
 * root axes to world, and head_root_metres anchors them to the presentation
 * head pose. role_mask is all-or-nothing for the active calibration roles. */
typedef struct vr_input_fbt_visual_snapshot_s
{
	unsigned int role_mask;
	float body_yaw_degrees;
	vec3_t head_root_metres;
	vec3_t tracker_root_metres[VR_FBT_ROLE_COUNT];
	vec3_t target_root_metres[VR_FBT_ROLE_COUNT];
	/* Endpoints of the tracker/target local X/Y/Z orientation cues. */
	vec3_t tracker_axis_root_metres[VR_FBT_ROLE_COUNT][3];
	vec3_t target_axis_root_metres[VR_FBT_ROLE_COUNT][3];
} vr_input_fbt_visual_snapshot_t;

qboolean VR_InputFBTCalibrationVisualSnapshot (const vrxr_frame_t *frame,
	vr_input_fbt_visual_snapshot_t *snapshot);

#endif /* QUAKE_VR_INPUT_H */
