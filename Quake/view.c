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
// view.c -- player eye positioning

#include "quakedef.h"
#include "view.h"
#include "world.h"
#include "vr_aim.h"
#include "vr_locomotion.h"
#include "vr_weapon_calibration.h"
#include "vr_input.h"

/*

The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.

*/

cvar_t scr_ofsx = {"scr_ofsx", "0", CVAR_NONE};
cvar_t scr_ofsy = {"scr_ofsy", "0", CVAR_NONE};
cvar_t scr_ofsz = {"scr_ofsz", "0", CVAR_NONE};

cvar_t cl_rollspeed = {"cl_rollspeed", "200", CVAR_NONE};
cvar_t cl_rollangle = {"cl_rollangle", "2.0", CVAR_ARCHIVE_GAME};

cvar_t cl_bob = {"cl_bob", "0.02", CVAR_ARCHIVE_GAME};
cvar_t cl_bobcycle = {"cl_bobcycle", "0.6", CVAR_NONE};
cvar_t cl_bobup = {"cl_bobup", "0.5", CVAR_NONE};

cvar_t v_kicktime = {"v_kicktime", "0.5", CVAR_NONE};
cvar_t v_kickroll = {"v_kickroll", "0.6", CVAR_NONE};
cvar_t v_kickpitch = {"v_kickpitch", "0.6", CVAR_NONE};
cvar_t v_gunkick = {"v_gunkick", "1", CVAR_ARCHIVE_GAME}; // johnfitz

cvar_t v_autopitch = {"v_autopitch", "0", CVAR_ARCHIVE_GAME};

cvar_t v_iyaw_cycle = {"v_iyaw_cycle", "2", CVAR_NONE};
cvar_t v_iroll_cycle = {"v_iroll_cycle", "0.5", CVAR_NONE};
cvar_t v_ipitch_cycle = {"v_ipitch_cycle", "1", CVAR_NONE};
cvar_t v_iyaw_level = {"v_iyaw_level", "0.3", CVAR_NONE};
cvar_t v_iroll_level = {"v_iroll_level", "0.1", CVAR_NONE};
cvar_t v_ipitch_level = {"v_ipitch_level", "0.3", CVAR_NONE};

cvar_t v_idlescale = {"v_idlescale", "0", CVAR_NONE};

cvar_t crosshair = {"crosshair", "1", CVAR_ARCHIVE_GAME};
cvar_t crosshair_def = {"crosshair_def", "0", CVAR_ARCHIVE_GAME};
cvar_t crosshair_size = {"crosshair_size", "20", CVAR_ARCHIVE_GAME};
cvar_t crosshair_color = {"crosshair_color", "0", CVAR_ARCHIVE_GAME};
cvar_t crosshair_alpha = {"crosshair_alpha", "1", CVAR_ARCHIVE_GAME};

cvar_t gl_cshiftpercent = {"gl_cshiftpercent", "100", CVAR_NONE};
cvar_t gl_cshiftpercent_contents = {"gl_cshiftpercent_contents", "100", CVAR_NONE}; // QuakeSpasm
cvar_t gl_cshiftpercent_damage = {"gl_cshiftpercent_damage", "100", CVAR_NONE};		// QuakeSpasm
cvar_t gl_cshiftpercent_bonus = {"gl_cshiftpercent_bonus", "100", CVAR_NONE};		// QuakeSpasm
cvar_t gl_cshiftpercent_powerup = {"gl_cshiftpercent_powerup", "100", CVAR_NONE};	// QuakeSpasm

cvar_t r_viewmodel_quake = {"r_viewmodel_quake", "0", CVAR_ARCHIVE_GAME};

// Inherited Quakespasm VR scale/floor/comfort settings; keep their names and flags.
cvar_t vr_world_scale = {"vr_world_scale", "1.0", CVAR_ARCHIVE};
cvar_t vr_floor_offset = {"vr_floor_offset", "-16", CVAR_ARCHIVE};
cvar_t vr_hud_scale = {"vr_hud_scale", "0.025", CVAR_ARCHIVE};
/* Request eye mode on capable runtimes; policy keeps full-rate shading until
 * gaze is valid and stable. Fixed foveation remains an explicit menu choice. */
cvar_t vr_eye_tracking = {"vr_eye_tracking", "1", CVAR_ARCHIVE};
cvar_t vr_foveation = {"vr_foveation", "2", CVAR_ARCHIVE};
cvar_t vr_mirror = {"vr_mirror", "1", CVAR_ARCHIVE}; // 0=off, 1=left eye, 2=right eye
cvar_t vr_hidden_area = {"vr_hidden_area", "1", CVAR_ARCHIVE};
cvar_t vr_viewkick = {"vr_viewkick", "0", CVAR_NONE};
cvar_t vr_aimmode = {"vr_aimmode", "7", CVAR_ARCHIVE};
cvar_t vr_deadzone = {"vr_deadzone", "30", CVAR_ARCHIVE};
static cvar_t vr_gunangle = {"vr_gunangle", "32", CVAR_ARCHIVE};
cvar_t vr_gunmodelpitch = {"vr_gunmodelpitch", "0", CVAR_ARCHIVE};
cvar_t vr_gunmodelscale = {"vr_gunmodelscale", "1.0", CVAR_ARCHIVE};
cvar_t vr_gunmodely = {"vr_gunmodely", "0", CVAR_ARCHIVE};

// These describe the saved V_CalcRefdef base, including when paused. Do not
// subtract a newly received viewheight from a base prepared with an older one.
static float base_viewheight;
static qboolean base_player_view, base_angles_valid;
static vec3_t base_aim_angles;

// Keep donor cl.viewangles as input/command aim. The inherited resolver also
// needs an independent visual view and the preceding aim/head sample.
static vec3_t tracked_view_angles, tracked_previous_aim, tracked_previous_orientation;
static vec3_t tracked_raw_angles, tracked_withheld_aim;
static float tracked_yaw;
static float tracked_local_yaw;
static vec3_t prediction_view_offset;
static qboolean tracked_aim_ready;
static qboolean tracked_controller_history_valid;
static qboolean prediction_view_offset_applied;
/* View-owner lifetime for the body-relative eye. Command samples can stop
 * temporarily on focus loss without returning the eye to positional tracking. */
static qboolean tracked_body_anchor;
static qboolean tracked_viewmodel_active;
static qboolean tracked_viewmodel_pose_applied;
static float view_stair_delta;
/* Valid only between XR view preparation and completion/abort of that frame. */
static qboolean tracked_weapon_collision_frame_valid;
static vec3_t tracked_weapon_collision_offset;
static qboolean tracked_reference_pending, tracked_readback_yaw, tracked_server_yaw_pending;
static float tracked_server_yaw;
static qboolean tracked_server_yaw_from_setangle;

/* Immutable presentation entities are prepared on the main thread after the
 * stereo view pose, then consumed by Vulkan render tasks. */
static entity_t akimbo_pair_entities[2];
static qmodel_t *akimbo_pair_models[2];
static aliashdr_t *akimbo_pair_geometry[2];
static vec3_t akimbo_pair_collision_offsets[2];
static const mod_akimbo_pair_recipe_t *akimbo_source_recipe;
static qmodel_t *akimbo_source_model;
static aliashdr_t *akimbo_source_geometry;
static int akimbo_source_modelindex, akimbo_source_frame, akimbo_source_skin;
static int akimbo_source_prev_frame, akimbo_source_snap_frames;
static double akimbo_source_frame_change_time, akimbo_source_frame_duration;
static uint64_t akimbo_sample_id;
static qboolean akimbo_pair_prepared;
static qboolean akimbo_pair_frozen;
static entity_t held_melee_entity;
static qmodel_t *held_melee_model, *held_melee_source_model;
static aliashdr_t *held_melee_geometry;
static uint64_t held_melee_sample_id;
static qboolean held_melee_prepared;
static const mod_held_melee_recipe_t *held_melee_recipe;
static int held_melee_hand;
static vec3_t held_melee_edge_offsets[2], held_melee_collision_offset;

int V_TrackedAimMode (void)
{
	return isfinite (vr_aimmode.value) && vr_aimmode.value >= 1 && vr_aimmode.value <= 7 ? (int)vr_aimmode.value : VR_AIMMODE_HEAD_MYAW;
}

static void V_TrackedAimModeChanged (cvar_t *var)
{
	VR_InputInvalidateMotion ();
	// Source VR_AimMode_f clears mode-specific requests, not pose history.
	tracked_readback_yaw = tracked_server_yaw_pending = false;
	if (V_TrackedAimMode () == VR_AIMMODE_CONTROLLER)
	{
		VectorCopy (vec3_origin, tracked_withheld_aim);
		VectorCopy (cl.viewangles, tracked_previous_aim);
	}
	else if (tracked_controller_history_valid && V_UseTrackedView () &&
		cls.signon == SIGNONS && !cls.demoplayback && !cl.intermission)
	{
		/* Seed the inherited mode from hand aim without changing the normal
		 * controller movement basis or defeating a native angle lock. */
		tracked_previous_aim[ROLL] = 0;
		if (CL_AngleLocked ())
			VectorSubtract (tracked_previous_aim, cl.viewangles, tracked_withheld_aim);
		else
		{
			VectorCopy (tracked_previous_aim, cl.viewangles);
			VectorCopy (vec3_origin, tracked_withheld_aim);
		}
	}
	else if (tracked_controller_history_valid)
		/* Discarded hand history must not become fictitious input motion
		 * when a blended mode resumes after tracking loss. */
		VectorAdd (cl.viewangles, tracked_withheld_aim, tracked_previous_aim);
	tracked_controller_history_valid = false;
}

static void V_TrackedDeadzoneChanged (cvar_t *var)
{
	const float value = isfinite (var->value) ? CLAMP (0.0f, var->value, 70.0f) : 30.0f;
	if (var->value != value)
		Cvar_SetValueQuick (var, value);
}

static void V_RemovePredictionViewOffset (void)
{
	if (!prediction_view_offset_applied)
		return;
	VectorSubtract (r_refdef.vieworg, prediction_view_offset, r_refdef.vieworg);
	VectorCopy (vec3_origin, prediction_view_offset);
	prediction_view_offset_applied = false;
}

const float *V_GetPredictionViewOffset (void)
{
	return prediction_view_offset;
}

static void V_ApplyPredictionViewOffset (qboolean camera_eligible)
{
	vec3_t offset;
	int axis;

	for (axis = 0; axis < 3; axis++)
		if (!isfinite (r_refdef.vieworg[axis]))
		{
			CL_EvaluatePredictionViewOffset (offset, false);
			return;
		}
	if (!CL_EvaluatePredictionViewOffset (offset, camera_eligible))
		return;
	VectorAdd (r_refdef.vieworg, offset, r_refdef.vieworg);
	VectorCopy (offset, prediction_view_offset);
	prediction_view_offset_applied = true;
}

void V_ResetTrackedAim (void)
{
	V_RemovePredictionViewOffset ();
	CL_ResetPredictionSmoothing ();
	V_ClearWeaponCollisionPresentation ();
	VR_WeaponCalibrationAdjustCancel ();
	tracked_local_yaw = 0;
	tracked_body_anchor = false;
	/* A new client/map identity cannot inherit a previous paused camera base. */
	R_ResetTrackedBodyCamera ();
	tracked_viewmodel_active = false;
	tracked_viewmodel_pose_applied = false;
	view_stair_delta = 0;
	VR_InputInvalidateMotion ();
	tracked_aim_ready = false;
	tracked_controller_history_valid = false;
	base_player_view = base_angles_valid = false;
	VectorCopy (vec3_origin, tracked_withheld_aim);
	tracked_reference_pending = tracked_readback_yaw = tracked_server_yaw_pending = false;
}

void V_RebaseTrackedAim (void)
{
	// A runtime origin change is not a new game. Retain mapped head and aim
	// histories until a valid pose can establish the replacement yaw basis.
	tracked_reference_pending = true;
	VR_InputInvalidateMotion ();
}

void V_SetTrackedAngles (const vec3_t angles)
{
	// An authoritative absolute angle supersedes uncommitted local turning.
	tracked_local_yaw = 0;
	tracked_controller_history_valid = false;
	VR_InputInvalidateMotion ();
	VectorCopy (angles, tracked_view_angles);
	VectorCopy (angles, tracked_previous_aim);
	VectorCopy (vec3_origin, tracked_withheld_aim);
}

void V_PushTrackedYaw (void)
{
	if (!vulkan_globals.stereo_active)
		return;
	tracked_local_yaw = 0;
	VR_InputInvalidateMotion ();
	if (V_TrackedAimMode () == VR_AIMMODE_CONTROLLER)
	{
		tracked_server_yaw = tracked_aim_ready ? tracked_view_angles[YAW] : cl.viewangles[YAW];
		tracked_server_yaw_pending = true;
		tracked_server_yaw_from_setangle = false;
	}
	else
		tracked_readback_yaw = true;
}

void V_RequestTrackedServerYaw (float yaw)
{
	if (!vulkan_globals.stereo_active || V_TrackedAimMode () != VR_AIMMODE_CONTROLLER)
		return;
	// Classify only after the whole message: a later stat can hide the weapon.
	tracked_server_yaw_pending = cl.stats[STAT_WEAPON] != 0 && !cl.intermission;
	tracked_server_yaw = yaw;
	tracked_server_yaw_from_setangle = true;
}

void V_ValidateTrackedServerYaw (void)
{
	// Also run at message completion: hidden/revealed weapons can arrive
	// between rendered frames, after the donor angle lock has expired.
	if (cl.intermission || (tracked_server_yaw_from_setangle && !cl.stats[STAT_WEAPON]))
		tracked_server_yaw_pending = false;
}

void V_TrackedAngleDelta (const vec3_t delta)
{
	VR_InputInvalidateMotion ();
	for (int i = 0; i < 3; ++i)
	{
		tracked_view_angles[i] += delta[i];
		tracked_previous_aim[i] += delta[i];
	}
	if (V_TrackedAimMode () == VR_AIMMODE_CONTROLLER)
	{
		tracked_yaw += delta[YAW];
		tracked_previous_orientation[YAW] += delta[YAW];
		if (tracked_server_yaw_pending)
			tracked_server_yaw += delta[YAW];
	}
}

void V_UpdateTrackedAim (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	V_ValidateTrackedServerYaw ();
	if (!frame || !frame->should_render || !frame->devices[0].valid || cls.signon != SIGNONS || cls.demoplayback || cl.intermission)
		return;
	vec3_t orientation, aim;
	if (!VR_AimPoseAngles (frame->devices[0].matrix, 0, tracked_raw_angles))
	{
		return;
	}
	const int mode = V_TrackedAimMode ();
	VectorCopy (tracked_raw_angles, orientation);
	if (!tracked_aim_ready)
	{
		// Establish a fresh yaw basis without replaying motion from another
		// mode/session. Mouse/head modes retain the existing input aim offset.
		tracked_yaw = (mode == VR_AIMMODE_CONTROLLER ? cl.viewangles[YAW] : 0) - orientation[YAW];
		orientation[YAW] += tracked_yaw;
		VectorCopy (orientation, tracked_previous_orientation);
		VectorCopy (cl.viewangles, tracked_previous_aim);
		VectorCopy (cl.viewangles, tracked_view_angles);
		tracked_aim_ready = true;
	}
	else
	{
		if (tracked_reference_pending)
			tracked_yaw = tracked_previous_orientation[YAW] - tracked_raw_angles[YAW];
		orientation[YAW] += tracked_yaw;
	}
	tracked_reference_pending = false;
	if (mode == VR_AIMMODE_CONTROLLER && tracked_server_yaw_pending)
	{
		tracked_yaw += tracked_server_yaw - orientation[YAW];
		orientation[YAW] = tracked_server_yaw;
		tracked_server_yaw_pending = false;
	}
	if (mode != VR_AIMMODE_CONTROLLER && tracked_readback_yaw)
	{
		// Preserve source ordering: this frame resolves with the old mapped
		// orientation; the updated alignment enters the following sample.
		tracked_yaw = tracked_view_angles[YAW] - (orientation[YAW] - tracked_yaw);
		tracked_readback_yaw = false;
	}
	// Apply local turning after any reference-space alignment. Retaining this
	// delta until here prevents a rebase from silently cancelling a snap turn.
	tracked_yaw += tracked_local_yaw;
	orientation[YAW] += tracked_local_yaw;
	tracked_local_yaw = 0;
	VectorAdd (cl.viewangles, tracked_withheld_aim, aim);
	const float deadzone = isfinite (vr_deadzone.value) ? vr_deadzone.value : 30.f;
	VR_AimResolve (mode, deadzone, orientation, tracked_previous_orientation, tracked_previous_aim, NULL, aim, tracked_view_angles);
	// Controller muzzle/weapon aim remains independent of movement command
	// angles. The input adapter prepares those without overwriting native aim.
	if (mode != VR_AIMMODE_CONTROLLER)
	{
		if (CL_AngleLocked ())
			VectorSubtract (aim, cl.viewangles, tracked_withheld_aim);
		else
		{
			// Publish accumulated physical aim once when the server lock ends.
			VectorCopy (aim, cl.viewangles);
			VectorCopy (vec3_origin, tracked_withheld_aim);
		}
	}
	VectorCopy (orientation, tracked_previous_orientation);
	if (mode != VR_AIMMODE_CONTROLLER)
		VectorCopy (aim, tracked_previous_aim);
	else
	{
		const int dominant = VR_InputDominantPhysicalHand ();
		vec3_t hand_aim;
		/* Preserve the last real hand sample across temporary tracking loss. */
		if (dominant >= 0 && dominant < 2 && frame->devices[dominant + 1].tracked &&
			V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, dominant, hand_aim))
		{
			VectorCopy (hand_aim, tracked_previous_aim);
			tracked_controller_history_valid = true;
		}
	}
}

const float *V_TrackedViewAngles (void)
{
	return tracked_aim_ready && V_UseTrackedView () && !cls.demoplayback && !cl.intermission ? tracked_view_angles : cl.viewangles;
}

qboolean V_TrackedPresentationYaw (float *yaw)
{
	if (yaw)
		*yaw = 0.0f;
	if (!yaw || !tracked_aim_ready || !base_angles_valid || !V_UseTrackedView () ||
		cls.demoplayback || cl.intermission)
		return false;
	/* CL_SendCmd precedes the screen update that consumes a local turn.
	 * Every VR_AimResolve mode adds that delta to visible yaw, so include it
	 * in a pose sent now without suppressing continuous-turn tracking. */
	*yaw = tracked_view_angles[YAW] - tracked_raw_angles[YAW] + tracked_local_yaw;
	return isfinite (*yaw);
}

qboolean V_ApplyTrackedView (vec3_t angles, float *tracking_yaw)
{
	if (!angles || !V_TrackedPresentationYaw (tracking_yaw))
		return false;
	// Replace only the input-aim contribution of the prepared base. Its kick
	// and idle contributions survive; a paused base receives the latest head.
	for (int i = 0; i < 3; ++i)
		angles[i] += tracked_view_angles[i] - base_aim_angles[i];
	// Chase stores the visual input used by its collision-traced base, so
	// paused tracking refreshes orientation without discarding that result.
	return true;
}

qboolean V_TrackedSessionActive (void)
{
	return GL_OpenXRFrame () != NULL;
}

/* Read the same completed tracking sample used by the input adapter. The
 * mapping belongs to this view owner; callers must not maintain another yaw. */
qboolean V_TrackedMappingYaw (float *yaw)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	if (!yaw || !frame || !frame->focused || !tracked_aim_ready || tracked_reference_pending ||
		!frame->devices[0].valid || cls.signon != SIGNONS || cls.demoplayback || cl.intermission ||
		!isfinite (tracked_yaw + tracked_local_yaw))
		return false;
	*yaw = tracked_yaw + tracked_local_yaw;
	return true;
}

/* Once the admitted private movement path has begun body-relative tracking,
 * temporary focus/pose loss or a mode switch must not reapply the historical
 * horizontal HMD displacement. Command tags only start this view ownership;
 * they are not receipts for server movement or prediction. */
qboolean V_TrackedBodyOwnsRoomscale (void)
{
	float viewheight;
	if (cl.protocol_qsvr != QSVR_PROTOCOL_PINNED)
	{
		tracked_body_anchor = false;
		return false;
	}
	if (!cls.demoplayback && !cl.intermission &&
		V_TrackedAimMode () == VR_AIMMODE_CONTROLLER &&
		(cl.pendingcmd.vr_active || cl.cmd.vr_active))
		tracked_body_anchor = true;
	return tracked_body_anchor && !cls.demoplayback && !cl.intermission &&
		V_TrackedPlayerBase (&viewheight);
}

float V_VRGunAngle (void)
{
	return isfinite (vr_gunangle.value) ? vr_gunangle.value : 32.0f;
}

static qboolean V_TrackedHandAnglesForYaw (const vrxr_frame_t *frame,
	int physical_hand, float yaw, vec3_t angles)
{
	const vrxr_device_t *hand;
	if (!frame || !angles || physical_hand < 0 || physical_hand > 1 || !isfinite (yaw))
		return false;
	hand = &frame->devices[physical_hand + 1];
	if (!hand->valid || hand->kind != VRXR_DEVICE_HAND || hand->hand != physical_hand)
		return false;
	return VR_LocomotionHandAngles (hand->matrix, yaw, V_VRGunAngle (), angles);
}

qboolean V_TrackedMovementAngles (int mode, int physical_offhand, vec3_t angles)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	float tracking_yaw;
	if (!angles || !V_TrackedMappingYaw (&tracking_yaw))
		return false;
	if (mode == VR_MOVEMENT_MODE_FOLLOW_HEAD)
		return VR_AimPoseAngles (frame->devices[0].matrix, tracking_yaw, angles);
	if ((mode != VR_MOVEMENT_MODE_FOLLOW_HAND && mode != VR_MOVEMENT_MODE_RAW_INPUT) ||
		physical_offhand < 0 || physical_offhand > 1)
		return false;
	return V_TrackedHandAnglesForYaw (frame, physical_offhand, tracking_yaw, angles);
}

qboolean V_TrackedPresentationHandAngles (int physical_hand, vec3_t angles)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	float presentation_yaw;
	return angles && frame && frame->focused && V_TrackedPresentationYaw (&presentation_yaw) &&
		V_TrackedHandAnglesForYaw (frame, physical_hand, presentation_yaw, angles);
}

static qboolean V_TrackedHandBodyOffsetForYaw (int physical_hand, float yaw, vec3_t out)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const vrxr_device_t *hand;
	float base_viewheight, head_eye_height;
	vec3_t head_position, hand_position;

	if (out)
		VectorCopy (vec3_origin, out);
	if (!out || physical_hand < 0 || physical_hand > 1 || !frame ||
		!frame->should_render || !frame->devices[0].valid ||
		!isfinite (yaw) || !V_TrackedPlayerBase (&base_viewheight))
		return false;
	hand = &frame->devices[physical_hand + 1];
	if (!hand->valid || hand->kind != VRXR_DEVICE_HAND || hand->hand != physical_hand)
		return false;
	for (int i = 0; i < 3; ++i)
	{
		head_position[i] = frame->devices[0].matrix[i][3];
		hand_position[i] = hand->matrix[i][3];
		if (!isfinite (head_position[i]) || !isfinite (hand_position[i]))
			return false;
	}
	if (!R_TrackedHeadEyeHeight (base_viewheight, &head_eye_height))
		return false;
	return VR_LocomotionHandBodyOffset (head_position, hand_position, yaw,
		V_VRUnitsPerMetre (), head_eye_height, out);
}

qboolean V_TrackedHandBodyOffset (int physical_hand, vec3_t out)
{
	float tracking_yaw;
	if (out)
		VectorCopy (vec3_origin, out);
	return out && V_TrackedMappingYaw (&tracking_yaw) &&
		V_TrackedHandBodyOffsetForYaw (physical_hand, tracking_yaw, out);
}

qboolean V_TrackedPresentationHandBodyOffset (int physical_hand, vec3_t out)
{
	float presentation_yaw;
	vec3_t head_offset;
	if (out)
		VectorCopy (vec3_origin, out);
	if (!out || !V_TrackedPresentationYaw (&presentation_yaw) ||
		!R_TrackedHeadBodyOffset (head_offset) ||
		!V_TrackedHandBodyOffsetForYaw (physical_hand, presentation_yaw, out))
	{
		if (out)
			VectorCopy (vec3_origin, out);
		return false;
	}
	out[0] += head_offset[0];
	out[1] += head_offset[1];
	if (!isfinite (out[0]) || !isfinite (out[1]) || !isfinite (out[2]))
	{
		VectorCopy (vec3_origin, out);
		return false;
	}
	return true;
}

qboolean V_TrackedPresentationHandWorldPose (int physical_hand,
	vec3_t origin, vec3_t hand_angles)
{
	vec3_t body_offset, angles, position;

	if (!origin || !hand_angles || !cl.entities || cl.viewentity <= 0 ||
		cl.viewentity >= cl.num_entities ||
		!V_TrackedPresentationHandBodyOffset (physical_hand, body_offset) ||
		!V_TrackedPresentationHandAngles (physical_hand, angles))
		return false;

	VectorAdd (cl.entities[cl.viewentity].origin, body_offset, position);
	position[2] += view_stair_delta;
	for (int i = 0; i < 3; ++i)
		if (!isfinite (position[i]) || !isfinite (angles[i]))
			return false;
	VectorCopy (position, origin);
	VectorCopy (angles, hand_angles);
	return true;
}

static void V_UpdateTrackedViewmodel (qboolean refdef_updated)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const qboolean controller_vr = frame &&
		V_TrackedAimMode () == VR_AIMMODE_CONTROLLER && cls.signon == SIGNONS &&
		!cls.demoplayback && !cl.intermission && !con_forcedup &&
		cl.entities && cl.viewentity > 0 && cl.viewentity < cl.num_entities;

	tracked_viewmodel_active = false;
	if (controller_vr)
	{
		const int dominant = VR_InputDominantPhysicalHand ();
		vec3_t hand_angles, model_angles, origin;
		if (V_TrackedPresentationHandWorldPose (dominant, origin, hand_angles) &&
			VR_LocomotionHandRotToViewmodelAngles (hand_angles, model_angles,
			vr_gunmodelpitch.value))
		{
			VR_WeaponCalibrationAdjustPresentation (origin, model_angles);
			VectorCopy (origin, cl.viewent.origin);
			VectorCopy (model_angles, cl.viewent.angles);
			tracked_viewmodel_active = true;
			tracked_viewmodel_pose_applied = true;
			return;
		}
		// Once a tracked pose has reached the entity, do not render that stale
		// transform during transient focus or controller-pose loss.
		return;
	}

	// A desktop refdef restores the native gun transform. Retain ownership
	// through forced-up/loading frames until that restoration actually occurs.
	if (refdef_updated)
		tracked_viewmodel_pose_applied = false;
}

void V_ClearAkimboPair (void)
{
	held_melee_prepared = false;
	held_melee_recipe = NULL;
	held_melee_model = held_melee_source_model = NULL;
	held_melee_geometry = NULL;
	held_melee_sample_id = 0;
	held_melee_hand = -1;
	memset (held_melee_edge_offsets, 0, sizeof (held_melee_edge_offsets));
	VectorCopy (vec3_origin, held_melee_collision_offset);
	akimbo_pair_prepared = false;
	akimbo_pair_frozen = false;
	akimbo_source_recipe = NULL;
	akimbo_source_model = NULL;
	akimbo_source_geometry = NULL;
	akimbo_source_modelindex = 0;
	akimbo_source_frame = -1;
	akimbo_source_skin = -1;
	akimbo_sample_id = 0;
	memset (akimbo_pair_models, 0, sizeof (akimbo_pair_models));
	memset (akimbo_pair_geometry, 0, sizeof (akimbo_pair_geometry));
	memset (akimbo_pair_collision_offsets, 0,
		sizeof (akimbo_pair_collision_offsets));
}

static qboolean V_AkimboFrameDevicesValid (const vrxr_frame_t *frame)
{
	const vrxr_device_t *head;
	if (!frame || !frame->sample_id || !frame->focused || !frame->should_render)
		return false;
	head = &frame->devices[0];
	if (!head->valid || !head->tracked || head->kind != VRXR_DEVICE_HEAD || head->hand != -1)
		return false;
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 4; ++column)
			if (!isfinite (head->matrix[row][column]))
				return false;
	for (int hand = 0; hand < 2; ++hand)
	{
		const vrxr_device_t *device = &frame->devices[hand + 1];
		if (!VR_InputPhysicalHandAccepted (frame, hand) || !device->valid ||
			!device->tracked || device->kind != VRXR_DEVICE_HAND || device->hand != hand)
			return false;
		for (int row = 0; row < 3; ++row)
			for (int column = 0; column < 4; ++column)
				if (!isfinite (device->matrix[row][column]))
					return false;
	}
	return true;
}

qboolean V_AkimboRecipeSupported (const char *source_model)
{
	const mod_akimbo_pair_recipe_t *recipe =
		Mod_GetAkimboPairRecipe (source_model);
	if (!recipe)
		return false;
	if (!strcmp (recipe->game, "qbj3") &&
		!strcmp (recipe->source, "progs/v_tnailgun.mdl"))
		return cl.vr_qbj3_akimbo_supported;
	if (!strcmp (recipe->game, "qbj3") &&
		!strcmp (recipe->source, "progs/v_berserk.mdl"))
		return cl.vr_qbj3_berserk_akimbo_supported;
	if (!strcmp (recipe->game, "enyo") &&
		!strcmp (recipe->source, "progs/ee_v_smgs.mdl"))
		return cl.vr_enyo_akimbo_supported;
	if (!strcmp (recipe->game, "dwell") &&
		!strcmp (recipe->source, "progs/v_axeb.mdl"))
		return cl.vr_dwell_berserk_akimbo_supported &&
			(cl.vr_weapon_contact_mode & VR_WEAPON_CONTACT_CAP_MELEE) != 0 &&
			cl.vr_weapon_contact_profile == VR_WEAPON_CONTACT_PROFILE_DWELL;
	return false;
}

static qboolean V_AkimboRecipeIsDwellAxe (
	const mod_akimbo_pair_recipe_t *recipe)
{
	return recipe && !strcmp (recipe->game, "dwell") &&
		!strcmp (recipe->source, "progs/v_axeb.mdl");
}

static qboolean V_AkimboRecipeIsQBJ3Fist (
	const mod_akimbo_pair_recipe_t *recipe)
{
	return recipe && !strcmp (recipe->game, "qbj3") &&
		!strcmp (recipe->source, "progs/v_berserk.mdl");
}

/* This single-hand recipe reuses the pair loader's pinned split output and
 * the existing two-point ready-pose cache. A virtual-path override has no
 * generated provenance and must never acquire these source-specific points. */
static qboolean V_HeldMeleeGeometry (qmodel_t **model_out,
	aliashdr_t **geometry_out, stockaxe_edge_t *edge_out,
	const mod_held_melee_recipe_t **recipe_out)
{
	const int modelindex = cl.stats[STAT_WEAPON];
	const mod_held_melee_recipe_t *recipe;
	qmodel_t *source, *held;
	aliashdr_t *source_geometry, *geometry;

	if (model_out)
		*model_out = NULL;
	if (geometry_out)
		*geometry_out = NULL;
	if (edge_out)
		memset (edge_out, 0, sizeof (*edge_out));
	if (recipe_out)
		*recipe_out = NULL;
	if (!model_out || !geometry_out || !edge_out || !recipe_out || modelindex < 1 ||
		modelindex >= MAX_MODELS ||
		!V_UseTrackedView () || V_TrackedAimMode () != VR_AIMMODE_CONTROLLER ||
		cl.protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!VR_InputGestureMeleeActive () ||
		cl.stats[STAT_ACTIVEWEAPON] != 4096)
		return false;
	source = cl.model_precache[modelindex];
	if (!source || source->needload || source->type != mod_alias ||
		source != cl.viewent.model ||
		cl.viewent.skinnum < 0)
		return false;
	recipe = Mod_GetHeldMeleeRecipe (source->name);
	if (!recipe)
		return false;
	held = Mod_ForName (recipe->held, false);
	if (!held || held->needload || held->type != mod_alias ||
		!held->is_generated_akimbo_half ||
		strcmp (held->name, recipe->held))
		return false;
	/* Loading the generated model can move cached headers. Resolve the
	 * source geometry only after that load, as the pair path already does. */
	source_geometry = (aliashdr_t *)source->extradata[PV_QUAKE1];
	if (!source_geometry || source_geometry->poseverttype != PV_QUAKE1 ||
		source_geometry->numverts != recipe->source_vertices ||
		source_geometry->numtris != recipe->source_triangles ||
		source_geometry->numframes != recipe->frames ||
		cl.viewent.skinnum >= source_geometry->numskins ||
		source_geometry->frames[recipe->ready_frame].numposes != 1)
		return false;
	/* Generated held meshes retain their authored ready pose while native QC
	 * continues its ordinary attack sequence. */
	geometry = (aliashdr_t *)held->extradata[PV_QUAKE1];
	if (!geometry || geometry->poseverttype != PV_QUAKE1 ||
		geometry->numverts != recipe->vertices || geometry->numtris != recipe->triangles ||
		geometry->numframes != recipe->frames || geometry->numskins <= cl.viewent.skinnum ||
		geometry->frames[recipe->ready_frame].numposes != 1 ||
		geometry->frames[recipe->ready_frame].firstpose < 0 ||
		geometry->frames[recipe->ready_frame].firstpose >= geometry->numposes ||
		memcmp (source_geometry->scale, geometry->scale,
			sizeof (geometry->scale)) ||
		memcmp (source_geometry->scale_origin, geometry->scale_origin,
			sizeof (geometry->scale_origin)) ||
		!held->stockaxe_edge.valid)
		return false;
	*model_out = held;
	*geometry_out = geometry;
	*edge_out = held->stockaxe_edge;
	*recipe_out = recipe;
	return true;
}

/* Only the authored-left wrench adds a tracked-forward roll before reflection. */
static qboolean V_HeldMeleeModelAngles (const mod_held_melee_recipe_t *recipe,
	const vec3_t hand_angles, vec3_t out)
{
	const int hand = VR_InputDominantPhysicalHand ();
	if (!recipe || hand < 0 || hand > 1)
		return false;
	if (recipe->controller_roll == 0.0f)
		return VR_LocomotionHandRotToViewmodelAngles (hand_angles, out,
			vr_gunmodelpitch.value);
	return VR_LocomotionControllerRollViewmodelAngles (hand_angles,
		vr_gunmodelpitch.value, (hand == 0 ? -1 : 1) * recipe->controller_roll, out);
}

static qboolean V_HeldMeleeGeometryEdgeOffsets (entity_t *entity,
	const aliashdr_t *geometry, const stockaxe_edge_t *edge,
	vec3_t out_base, vec3_t out_tip)
{
	lerpdata_t lerpdata;
	vec3_t raw[2];
	float matrix[16];
	const float *points[2] = {edge->base, edge->tip};
	float *outputs[2] = {out_base, out_tip};

	for (int axis = 0; axis < 3; ++axis)
	{
		if (!isfinite (geometry->scale[axis]) || geometry->scale[axis] == 0.0f ||
			!isfinite (geometry->scale_origin[axis]))
			return false;
		for (int point = 0; point < 2; ++point)
			raw[point][axis] = (points[point][axis] -
				geometry->scale_origin[axis]) / geometry->scale[axis];
	}
	memset (&lerpdata, 0, sizeof (lerpdata));
	VectorCopy (entity->angles, lerpdata.angles);
	if (R_HeldMeleeMatrix (entity, geometry, &lerpdata,
		matrix) < 0)
		return false;
	for (int point = 0; point < 2; ++point)
	{
		for (int axis = 0; axis < 3; ++axis)
			outputs[point][axis] = matrix[axis] * raw[point][0] +
				matrix[4 + axis] * raw[point][1] +
				matrix[8 + axis] * raw[point][2] + matrix[12 + axis];
		for (int axis = 0; axis < 3; ++axis)
			if (!isfinite (outputs[point][axis]))
				return false;
	}
	return true;
}

/* Publish an ordinary alias entity before Vulkan draw tasks start. The
 * server and source viewent retain the QC model and its animation state. */
static void V_PrepareHeldMelee (const vrxr_frame_t *frame)
{
	const mod_held_melee_recipe_t *recipe;
	qmodel_t *held;
	aliashdr_t *geometry;
	stockaxe_edge_t edge;
	lerpdata_t pose;
	vec3_t hand_angles, model_angles, base_offset, tip_offset;
	vec3_t collision_offset = {0.0f, 0.0f, 0.0f};
	float matrix[16];
	const int dominant = VR_InputDominantPhysicalHand ();

	if (!frame || !frame->sample_id || !frame->focused ||
		!frame->should_render || !vulkan_globals.stereo_active ||
		!tracked_viewmodel_active || VR_WeaponCalibrationAdjustActive () ||
		key_dest != key_game || cl.stats[STAT_HEALTH] <= 0 ||
		dominant < 0 || dominant > 1 ||
		!VR_InputPhysicalHandAccepted (frame, dominant) ||
		!frame->devices[dominant + 1].valid ||
		!frame->devices[dominant + 1].tracked ||
		frame->devices[dominant + 1].kind != VRXR_DEVICE_HAND ||
		frame->devices[dominant + 1].hand != dominant ||
		!V_HeldMeleeGeometry (&held, &geometry, &edge, &recipe) ||
		!V_TrackedPresentationHandAngles (dominant, hand_angles) ||
		!V_HeldMeleeModelAngles (recipe, hand_angles, model_angles))
		return;

	held_melee_entity = cl.viewent;
	held_melee_entity.model = held;
	held_melee_entity.frame = recipe->ready_frame;
	held_melee_entity.lerp.prev_frame = recipe->ready_frame;
	held_melee_entity.lerp.frame_change_time = 0.0;
	held_melee_entity.lerp.frame_duration = 0.0;
	held_melee_entity.lerp.snap_frames = 0;
	held_melee_entity.lerp.movestep = false;
	VectorCopy (model_angles, held_melee_entity.angles);
	if (!V_HeldMeleeGeometryEdgeOffsets (&held_melee_entity,
		geometry, &edge, base_offset, tip_offset))
		return;
	if (VR_WeaponCollisionAuthorized ())
	{
		vec3_t torso_offset, torso, base, tip;
		float head_height;
		if (!cl.entities || cl.viewentity <= 0 ||
			cl.viewentity >= cl.num_entities ||
			!R_TrackedHeadBodyOffset (torso_offset) ||
			!R_TrackedHeadEyeHeight (cl.stats[STAT_VIEWHEIGHT], &head_height))
			return;
		VectorAdd (cl.entities[cl.viewentity].origin, torso_offset, torso);
		torso[2] += head_height + view_stair_delta;
		VectorAdd (held_melee_entity.origin, base_offset, base);
		VectorAdd (held_melee_entity.origin, tip_offset, tip);
		if (!CL_ResolveWeaponCollision (torso, held_melee_entity.origin,
			base, tip, collision_offset))
			return;
		VectorAdd (held_melee_entity.origin, collision_offset,
			held_melee_entity.origin);
	}
	R_SetupAliasFrame (&held_melee_entity, geometry, &pose);
	VectorCopy (held_melee_entity.origin, pose.origin);
	VectorCopy (held_melee_entity.angles, pose.angles);
	if (pose.pose1 != geometry->frames[recipe->ready_frame].firstpose ||
		pose.pose2 != pose.pose1 ||
		R_HeldMeleeMatrix (&held_melee_entity, geometry,
			&pose, matrix) < 0)
		return;
	held_melee_model = held;
	held_melee_recipe = recipe;
	held_melee_source_model = cl.viewent.model;
	held_melee_geometry = geometry;
	held_melee_sample_id = frame->sample_id;
	held_melee_hand = dominant;
	VectorCopy (base_offset, held_melee_edge_offsets[0]);
	VectorCopy (tip_offset, held_melee_edge_offsets[1]);
	VectorCopy (collision_offset, held_melee_collision_offset);
	held_melee_prepared = true;
}

entity_t *V_HeldMeleeEntity (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	return vulkan_globals.stereo_active && held_melee_prepared &&
		held_melee_recipe && cl.viewent.model &&
		Mod_GetHeldMeleeRecipe (cl.viewent.model->name) == held_melee_recipe &&
		frame && frame->sample_id == held_melee_sample_id && frame->focused &&
		frame->should_render && V_UseTrackedView () &&
		VR_InputDominantPhysicalHand () == held_melee_hand &&
		VR_InputPhysicalHandAccepted (frame, held_melee_hand) &&
		VR_InputGestureMeleeActive () &&
		cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		cl.stats[STAT_ACTIVEWEAPON] == 4096 &&
		cl.stats[STAT_WEAPON] > 0 && cl.stats[STAT_WEAPON] < MAX_MODELS &&
		cl.model_precache[cl.stats[STAT_WEAPON]] == held_melee_source_model &&
		held_melee_model && !held_melee_model->needload &&
		held_melee_entity.model == held_melee_model &&
		held_melee_geometry &&
		held_melee_model->extradata[PV_QUAKE1] ==
		(byte *)held_melee_geometry &&
		cl.viewent.model == held_melee_source_model &&
		held_melee_entity.netstate.scale == cl.viewent.netstate.scale &&
		held_melee_entity.skinnum == cl.viewent.skinnum ?
		&held_melee_entity : NULL;
}

qboolean V_HeldMeleeEdgeOffsets (vec3_t out_base, vec3_t out_tip,
	vec3_t out_collision)
{
	if (out_base)
		VectorCopy (vec3_origin, out_base);
	if (out_tip)
		VectorCopy (vec3_origin, out_tip);
	if (out_collision)
		VectorCopy (vec3_origin, out_collision);
	if (!out_base || !out_tip || !out_collision || !V_HeldMeleeEntity ())
		return false;
	VectorCopy (held_melee_edge_offsets[0], out_base);
	VectorCopy (held_melee_edge_offsets[1], out_tip);
	VectorCopy (held_melee_collision_offset, out_collision);
	return true;
}

qboolean V_HeldMeleeRawEdgeOffsets (const vec3_t hand_angles,
	vec3_t out_base, vec3_t out_tip)
{
	entity_t raw_entity;
	vec3_t model_angles;

	if (!hand_angles || !out_base || !out_tip || !V_HeldMeleeEntity () ||
		!held_melee_model->stockaxe_edge.valid ||
		!V_HeldMeleeModelAngles (held_melee_recipe, hand_angles, model_angles))
		return false;
	/* The prepared model and ready edge are validated above. Rebuild only its
	 * orientation from this command's hand sample, before stair and wall offsets. */
	raw_entity = held_melee_entity;
	VectorCopy (model_angles, raw_entity.angles);
	return V_HeldMeleeGeometryEdgeOffsets (&raw_entity,
		held_melee_geometry, &held_melee_model->stockaxe_edge,
		out_base, out_tip);
}

qboolean V_HeldMeleeRenderEntity (const entity_t *e)
{
	return e && e == V_HeldMeleeEntity ();
}

qboolean V_AkimboRecipeUsesPairedCollision (const char *source_model)
{
	const mod_akimbo_pair_recipe_t *recipe = Mod_GetAkimboPairRecipe (source_model);
	return recipe &&
		((!strcmp (recipe->game, "qbj3") &&
			!strcmp (recipe->source, "progs/v_tnailgun.mdl")) ||
		V_AkimboRecipeIsQBJ3Fist (recipe) ||
		(!strcmp (recipe->game, "enyo") &&
			!strcmp (recipe->source, "progs/ee_v_smgs.mdl")) ||
		V_AkimboRecipeIsDwellAxe (recipe));
}

/* Dwell held-angle matrices copied from quakespasm-openvr/Quake/vr.c:6265-6275
 * (vr_dwell_fists.correction), calibrated for its split v_axeb model. */
static const float dwell_akimbo_viewmodel_correction[2][3][3] = {
	{{-.294739431f, -.053186399f, -.954096366f},
	 {.009474343f, -.998563416f, .052738415f},
	 {-.955530693f, .006504655f, .294819919f}},
	{{-.503862298f, .131800285f, -.853669415f},
	 {.317507833f, -.890842899f, -.324942618f},
	 {-.803312866f, -.434773060f, .407014527f}}
};

/* Donor vr_qbj3_fists: compose the guard-pose correction through the same
 * model-angle owner used by both presentation and command anchors. */
static const float qbj3_akimbo_viewmodel_correction[2][3][3] = {
	{{.293235116f, -.608031630f, .737774155f},
	 {.608031630f, .714125870f, .346874297f},
	 {-.737774155f, .346874297f, .579109246f}},
	{{.295241133f, .603903037f, .740360585f},
	 {-.603903037f, .718431673f, -.345191327f},
	 {-.740360585f, -.345191327f, .576809459f}}
};

/* Frame-zero cutting edges from generated Dwell halves. These immutable raw
 * endpoints match the pinned v_axeb source CRC32 69c2bf5e and generated half
 * CRC32 values 8e5fd44b (left) and f3c035b6 (right). */
static const vec3_t dwell_akimbo_raw_edges[2][2] = {
	{{76.0f, 178.0f, 59.0f}, {51.0f, 183.0f, 68.0f}},
	{{77.0f, 117.0f, 71.0f}, {52.0f, 114.0f, 83.0f}}
};

qboolean V_AkimboModelAngles (const char *source_model, int physical_hand,
	const vec3_t raw_hand_angles, vec3_t out)
{
	const mod_akimbo_pair_recipe_t *recipe;
	vec3_t hand_angles, model_angles;
	qboolean converted;

	if (!out)
		return false;
	if (raw_hand_angles)
		VectorCopy (raw_hand_angles, hand_angles);
	VectorCopy (vec3_origin, out);
	if (!raw_hand_angles || physical_hand < 0 || physical_hand > 1)
		return false;

	recipe = Mod_GetAkimboPairRecipe (source_model);
	if (recipe && !strcmp (recipe->game, "dwell") &&
		!strcmp (recipe->source, "progs/v_axeb.mdl"))
		converted = VR_LocomotionCorrectedViewmodelAngles (hand_angles,
			vr_gunmodelpitch.value, dwell_akimbo_viewmodel_correction[physical_hand],
			model_angles);
	else if (recipe && !strcmp (recipe->game, "qbj3") &&
		!strcmp (recipe->source, "progs/v_berserk.mdl"))
		converted = VR_LocomotionCorrectedViewmodelAngles (hand_angles,
			vr_gunmodelpitch.value, qbj3_akimbo_viewmodel_correction[physical_hand],
			model_angles);
	else
		converted = VR_LocomotionHandRotToViewmodelAngles (hand_angles,
			model_angles, vr_gunmodelpitch.value);
	if (!converted || !isfinite (model_angles[0]) ||
		!isfinite (model_angles[1]) || !isfinite (model_angles[2]))
		return false;

	VectorCopy (model_angles, out);
	return true;
}

static qboolean V_AkimboSelectionValid (const vrxr_frame_t *frame,
	qmodel_t **source_out, int *modelindex_out,
	const mod_akimbo_pair_recipe_t **recipe_out)
{
	int modelindex = cl.stats[STAT_WEAPON];
	qmodel_t *source;
	const mod_akimbo_pair_recipe_t *recipe;
	if (source_out)
		*source_out = NULL;
	if (modelindex_out)
		*modelindex_out = 0;
	if (recipe_out)
		*recipe_out = NULL;
	if (!vulkan_globals.stereo_active || !V_AkimboFrameDevicesValid (frame) ||
		!V_TrackedViewmodelActive () || VR_WeaponCalibrationAdjustActive () ||
		cl.protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback ||
		cl.intermission || con_forcedup || !cl.worldmodel || cl.worldmodel->needload ||
		!cl.entities || cl.viewentity <= 0 || cl.viewentity >= cl.num_entities ||
		cl.stats[STAT_HEALTH] <= 0 || (cl.items & IT_INVISIBILITY) ||
		!r_drawentities.value || !r_drawviewmodel.value || chase_active.value ||
		V_TrackedViewmodelShouldHide () || modelindex < 1 || modelindex >= MAX_MODELS)
		return false;
	source = cl.model_precache[modelindex];
	if (!source || source->needload || source->type != mod_alias ||
		source != cl.viewent.model)
		return false;
	recipe = Mod_GetAkimboPairRecipe (source->name);
	if (!recipe || !V_AkimboRecipeSupported (source->name))
		return false;
	if (source_out)
		*source_out = source;
	if (modelindex_out)
		*modelindex_out = modelindex;
	if (recipe_out)
		*recipe_out = recipe;
	return true;
}

static qboolean V_AkimboHeaderTopologyMatches (const aliashdr_t *source,
	const aliashdr_t *half, const mod_akimbo_pair_recipe_t *recipe,
	int physical_hand)
{
	if (!source || !half || !recipe || physical_hand < 0 || physical_hand > 1 ||
		source->poseverttype != PV_QUAKE1 || half->poseverttype != PV_QUAKE1 ||
		source->numframes != recipe->source_frames ||
		half->numframes != source->numframes ||
		source->numverts != recipe->source_vertices ||
		half->numverts != recipe->half_vertices[physical_hand] ||
		source->numposes != half->numposes ||
		source->numskins != half->numskins || source->skinwidth != half->skinwidth ||
		source->skinheight != half->skinheight ||
		memcmp (source->scale, half->scale, sizeof (source->scale)) ||
		memcmp (source->scale_origin, half->scale_origin, sizeof (source->scale_origin)))
		return false;
	for (int frame = 0; frame < source->numframes; ++frame)
	{
		const maliasframedesc_t *source_frame = &source->frames[frame];
		const maliasframedesc_t *half_frame = &half->frames[frame];
		if (source_frame->firstpose < 0 || source_frame->numposes <= 0 ||
			source_frame->firstpose > source->numposes - source_frame->numposes ||
			!isfinite (source_frame->interval) || source_frame->frame != half_frame->frame ||
			source_frame->firstpose != half_frame->firstpose ||
			source_frame->numposes != half_frame->numposes ||
			source_frame->interval != half_frame->interval ||
			memcmp (source_frame->name, half_frame->name, sizeof (source_frame->name)))
			return false;
	}
	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite (source->scale[axis]) || !isfinite (source->scale_origin[axis]))
			return false;
	return true;
}

static qboolean V_AkimboEntityMatrixValid (entity_t *entity,
	const aliashdr_t *geometry)
{
	lerpdata_t lerpdata;
	float matrix[16];
	memset (&lerpdata, 0, sizeof (lerpdata));
	VectorCopy (entity->origin, lerpdata.origin);
	VectorCopy (entity->angles, lerpdata.angles);
	return R_AliasModelMatrix (entity, geometry, &lerpdata, matrix) >= 0;
}

static qboolean V_AkimboTransformRawPointForPair (const vec3_t model_angles,
	entity_t *entity, const aliashdr_t *geometry, const vec3_t raw_point,
	vec3_t out_local);

static qboolean V_AkimboTransformAnchorForPair (int physical_hand,
	const vec3_t model_angles, const mod_akimbo_pair_recipe_t *recipe,
	const aliashdr_t *source_geometry, entity_t *entity,
	const aliashdr_t *geometry, vec3_t out_local)
{
	vec3_t raw_anchor;
	if (out_local)
		VectorCopy (vec3_origin, out_local);
	if (!out_local || !model_angles || !recipe || !source_geometry || !entity ||
		!geometry || physical_hand < 0 || physical_hand > 1)
		return false;
	for (int axis = 0; axis < 3; ++axis)
	{
		const float scale = source_geometry->scale[axis];
		const float origin = source_geometry->scale_origin[axis];
		if (!isfinite (model_angles[axis]) || !isfinite (scale) ||
			scale == 0.0f || !isfinite (origin) ||
			!isfinite (recipe->source_anchors[physical_hand][axis]))
			return false;
		/* The recipe anchor uses decoded source MDL coordinates; the alias
		 * matrix expects the original compressed vertex coordinates. */
		raw_anchor[axis] =
			(recipe->source_anchors[physical_hand][axis] - origin) / scale;
		if (!isfinite (raw_anchor[axis]))
			return false;
	}
	return V_AkimboTransformRawPointForPair (model_angles, entity, geometry,
		raw_anchor, out_local);
}

static qboolean V_AkimboTransformRawPointForPair (const vec3_t model_angles,
	entity_t *entity, const aliashdr_t *geometry, const vec3_t raw_point,
	vec3_t out_local)
{
	lerpdata_t lerpdata;
	float matrix[16];
	if (out_local)
		VectorCopy (vec3_origin, out_local);
	if (!out_local || !model_angles || !entity || !geometry || !raw_point)
		return false;
	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite (model_angles[axis]) || !isfinite (raw_point[axis]))
			return false;
	memset (&lerpdata, 0, sizeof (lerpdata));
	VectorCopy (model_angles, lerpdata.angles);
	if (R_AliasModelMatrix (entity, geometry, &lerpdata, matrix) < 0)
		return false;
	out_local[0] = matrix[0] * raw_point[0] + matrix[4] * raw_point[1] +
		matrix[8] * raw_point[2] + matrix[12];
	out_local[1] = matrix[1] * raw_point[0] + matrix[5] * raw_point[1] +
		matrix[9] * raw_point[2] + matrix[13];
	out_local[2] = matrix[2] * raw_point[0] + matrix[6] * raw_point[1] +
		matrix[10] * raw_point[2] + matrix[14];
	if (!isfinite (out_local[0]) || !isfinite (out_local[1]) ||
		!isfinite (out_local[2]))
	{
		VectorCopy (vec3_origin, out_local);
		return false;
	}
	return true;
}

static qboolean V_AkimboTransformDwellEdgesForPair (int physical_hand,
	const vec3_t model_angles, entity_t *entity, const aliashdr_t *geometry,
	vec3_t out_base, vec3_t out_tip)
{
	if (out_base)
		VectorCopy (vec3_origin, out_base);
	if (out_tip)
		VectorCopy (vec3_origin, out_tip);
	if (!out_base || !out_tip || physical_hand < 0 || physical_hand > 1 ||
		!V_AkimboTransformRawPointForPair (model_angles, entity, geometry,
			dwell_akimbo_raw_edges[physical_hand][0], out_base) ||
		!V_AkimboTransformRawPointForPair (model_angles, entity, geometry,
			dwell_akimbo_raw_edges[physical_hand][1], out_tip))
	{
		if (out_base)
			VectorCopy (vec3_origin, out_base);
		if (out_tip)
			VectorCopy (vec3_origin, out_tip);
		return false;
	}
	return true;
}

/* Source animation deforms the palms as intended, but its authored lunge
 * must not translate a controller-held fist. Use the renderer's exact pose
 * selection and the shared draw matrix's linear part; the ready-pose command
 * anchors continue to use the unshifted pair transform. */
static qboolean V_QBJ3CompensateAnimatedPalm (qmodel_t *source, int hand,
	entity_t *entity, aliashdr_t *geometry)
{
	lerpdata_t lerpdata;
	float matrix[16];
	vec3_t ready, first, second, raw_delta, world_delta;

	memset (&lerpdata, 0, sizeof (lerpdata));
	R_SetupAliasFrame (entity, geometry, &lerpdata);
	if (lerpdata.pose1 < 0 || lerpdata.pose1 >= geometry->numposes ||
		lerpdata.pose2 < 0 || lerpdata.pose2 >= geometry->numposes ||
		!isfinite (lerpdata.blend) || lerpdata.blend < 0.0f ||
		lerpdata.blend > 1.0f ||
		!Mod_GetQBJ3BerserkPalmCentroid (source, hand, 0, ready) ||
		!Mod_GetQBJ3BerserkPalmCentroid (source, hand, lerpdata.pose1, first) ||
		!Mod_GetQBJ3BerserkPalmCentroid (source, hand, lerpdata.pose2, second))
		return false;
	VectorCopy (entity->origin, lerpdata.origin);
	VectorCopy (entity->angles, lerpdata.angles);
	if (R_AliasModelMatrix (entity, geometry, &lerpdata, matrix) < 0)
		return false;
	for (int axis = 0; axis < 3; ++axis)
		raw_delta[axis] = first[axis] +
			lerpdata.blend * (second[axis] - first[axis]) - ready[axis];
	for (int axis = 0; axis < 3; ++axis)
	{
		world_delta[axis] = matrix[axis] * raw_delta[0] +
			matrix[axis + 4] * raw_delta[1] +
			matrix[axis + 8] * raw_delta[2];
		if (!isfinite (world_delta[axis]) ||
			!isfinite (entity->origin[axis] - world_delta[axis]))
			return false;
	}
	VectorSubtract (entity->origin, world_delta, entity->origin);
	return true;
}

void V_PrepareAkimboPair (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const mod_akimbo_pair_recipe_t *recipe;
	qmodel_t *source;
	aliashdr_t *source_geometry;
	int modelindex;
	vec3_t pair_origins[2], pair_model_angles[2];
	qboolean qbj3_fists, freeze_frame;

	V_ClearAkimboPair ();
	V_PrepareHeldMelee (frame);
	if (!V_AkimboSelectionValid (frame, &source, &modelindex, &recipe) ||
		!Mod_AkimboPairUsesGeneratedHalves (source->name) ||
		!recipe->halves[0] || !recipe->halves[1] ||
		!isfinite (vr_gunmodelpitch.value))
		return;
	qbj3_fists = V_AkimboRecipeIsQBJ3Fist (recipe);
	freeze_frame = (V_AkimboRecipeIsDwellAxe (recipe) || qbj3_fists) &&
		VR_InputGestureMeleeActive ();

	/* Pair files and any selected geometry are synchronously prepared here,
	 * before the renderer can distribute its viewmodel work to tasks. */
	for (int hand = 0; hand < 2; ++hand)
	{
		qmodel_t *model = Mod_ForName (recipe->halves[hand], false);
		if (!model || model->needload || model->type != mod_alias ||
			strcmp (model->name, recipe->halves[hand]) ||
			((V_AkimboRecipeIsDwellAxe (recipe) || qbj3_fists) &&
				!model->is_generated_akimbo_half))
			return;
		akimbo_pair_models[hand] = model;
	}

	/* Loading either half may move cached alias headers. Reacquire all three
	 * classic headers only after both synchronous loads have completed. */
	source_geometry = (aliashdr_t *)source->extradata[PV_QUAKE1];
	if (!source_geometry || source_geometry->poseverttype != PV_QUAKE1 ||
		source_geometry->numframes != recipe->source_frames ||
		source_geometry->numverts != recipe->source_vertices ||
		cl.viewent.frame < 0 || cl.viewent.frame >= recipe->source_frames ||
		cl.viewent.skinnum < 0 || cl.viewent.skinnum >= source_geometry->numskins ||
		(qbj3_fists &&
			Mod_Extradata_CheckSkin (source, cl.viewent.skinnum) != source_geometry))
		return;
	for (int hand = 0; hand < 2; ++hand)
	{
		qmodel_t *model = akimbo_pair_models[hand];
		aliashdr_t *geometry;
		if (!model || model->needload || model->type != mod_alias)
			return;
		geometry = (aliashdr_t *)model->extradata[PV_QUAKE1];
		if (!V_AkimboHeaderTopologyMatches (source_geometry, geometry, recipe, hand))
			return;
		akimbo_pair_geometry[hand] = geometry;
		akimbo_pair_entities[hand] = cl.viewent;
		akimbo_pair_entities[hand].model = model;
		if (freeze_frame)
		{
			akimbo_pair_entities[hand].frame = 0;
			akimbo_pair_entities[hand].lerp.prev_frame = 0;
			akimbo_pair_entities[hand].lerp.frame_change_time = 0.0;
			akimbo_pair_entities[hand].lerp.frame_duration = 0.0;
			akimbo_pair_entities[hand].lerp.snap_frames = 0;
		}
		vec3_t hand_angles;
		if (!V_TrackedPresentationHandWorldPose (hand, pair_origins[hand], hand_angles) ||
			!V_AkimboModelAngles (source->name, hand, hand_angles,
				pair_model_angles[hand]))
			return;
		VectorCopy (pair_origins[hand], akimbo_pair_entities[hand].origin);
		VectorCopy (pair_model_angles[hand], akimbo_pair_entities[hand].angles);
	}

	const vrxr_frame_t *current_frame = GL_OpenXRFrame ();
	if (!V_AkimboSelectionValid (current_frame, &source, &modelindex, &recipe) ||
		source != cl.viewent.model || modelindex != cl.stats[STAT_WEAPON] ||
		!current_frame || frame->sample_id != current_frame->sample_id)
		return;
	for (int hand = 0; hand < 2; ++hand)
		if (!V_AkimboEntityMatrixValid (&akimbo_pair_entities[hand], akimbo_pair_geometry[hand]))
			return;

	if (V_AkimboRecipeUsesPairedCollision (recipe->source) &&
		VR_WeaponCollisionAuthorized () && key_dest == key_game)
	{
		vec3_t torso_offset, torso;
		float head_height;
		entity_t *player = &cl.entities[cl.viewentity];
		if (R_TrackedHeadBodyOffset (torso_offset) &&
			R_TrackedHeadEyeHeight (cl.stats[STAT_VIEWHEIGHT], &head_height))
		{
			VectorAdd (player->origin, torso_offset, torso);
			torso[2] += head_height + view_stair_delta;
			for (int hand = 0; hand < 2; ++hand)
			{
				vec3_t base, tip, delta;
				if (V_AkimboRecipeIsDwellAxe (recipe))
				{
					vec3_t edge_base, edge_tip;
					if (!V_AkimboTransformDwellEdgesForPair (hand,
						pair_model_angles[hand], &akimbo_pair_entities[hand],
						akimbo_pair_geometry[hand], edge_base, edge_tip))
						return;
					VectorAdd (pair_origins[hand], edge_base, base);
					VectorAdd (pair_origins[hand], edge_tip, tip);
				}
				else
				{
					vec3_t anchor;
					if (!V_AkimboTransformAnchorForPair (hand,
						pair_model_angles[hand], recipe, source_geometry,
						&akimbo_pair_entities[hand], akimbo_pair_geometry[hand], anchor))
						return;
					VectorCopy (pair_origins[hand], base);
					VectorAdd (base, anchor, tip);
				}
				/* A pair cannot keep an obstructed raw hand when the solve fails.
				 * The ordinary dominant-hand path remains available this frame. */
				if (!CL_ResolveWeaponCollision (torso, pair_origins[hand],
					base, tip, delta))
					return;
				VectorCopy (delta, akimbo_pair_collision_offsets[hand]);
				VectorAdd (pair_origins[hand], delta, pair_origins[hand]);
				VectorCopy (pair_origins[hand], akimbo_pair_entities[hand].origin);
			}
		}
	}
	if (qbj3_fists && !freeze_frame)
		for (int hand = 0; hand < 2; ++hand)
			if (!V_QBJ3CompensateAnimatedPalm (source, hand,
				&akimbo_pair_entities[hand], akimbo_pair_geometry[hand]))
				return;
	for (int hand = 0; hand < 2; ++hand)
		if (!V_AkimboEntityMatrixValid (&akimbo_pair_entities[hand],
			akimbo_pair_geometry[hand]))
			return;

	akimbo_source_recipe = recipe;
	akimbo_source_model = source;
	akimbo_source_geometry = source_geometry;
	akimbo_source_modelindex = modelindex;
	akimbo_source_frame = cl.viewent.frame;
	akimbo_source_skin = cl.viewent.skinnum;
	akimbo_source_prev_frame = cl.viewent.lerp.prev_frame;
	akimbo_source_snap_frames = cl.viewent.lerp.snap_frames;
	akimbo_source_frame_change_time = cl.viewent.lerp.frame_change_time;
	akimbo_source_frame_duration = cl.viewent.lerp.frame_duration;
	akimbo_sample_id = frame->sample_id;
	akimbo_pair_frozen = freeze_frame;
	akimbo_pair_prepared = true;
}

qboolean V_AkimboPairReady (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const mod_akimbo_pair_recipe_t *recipe;
	qmodel_t *source;
	int modelindex;
	if (!akimbo_pair_prepared || !V_AkimboSelectionValid (frame, &source, &modelindex, &recipe) ||
		frame->sample_id != akimbo_sample_id || source != akimbo_source_model ||
		recipe != akimbo_source_recipe ||
		source->extradata[PV_QUAKE1] != (byte *)akimbo_source_geometry ||
		modelindex != akimbo_source_modelindex || cl.viewent.frame != akimbo_source_frame ||
		cl.viewent.skinnum != akimbo_source_skin ||
		cl.viewent.lerp.prev_frame != akimbo_source_prev_frame ||
		cl.viewent.lerp.snap_frames != akimbo_source_snap_frames ||
		cl.viewent.lerp.frame_change_time != akimbo_source_frame_change_time ||
		cl.viewent.lerp.frame_duration != akimbo_source_frame_duration)
		return false;
	for (int hand = 0; hand < 2; ++hand)
		if (!recipe->halves[hand] || !akimbo_pair_models[hand] ||
			akimbo_pair_models[hand]->needload ||
			strcmp (akimbo_pair_models[hand]->name, recipe->halves[hand]) ||
			((V_AkimboRecipeIsDwellAxe (recipe) ||
				V_AkimboRecipeIsQBJ3Fist (recipe)) &&
				!akimbo_pair_models[hand]->is_generated_akimbo_half) ||
			akimbo_pair_entities[hand].model != akimbo_pair_models[hand] ||
			akimbo_pair_entities[hand].skinnum != cl.viewent.skinnum ||
			!akimbo_pair_geometry[hand] ||
			akimbo_pair_models[hand]->extradata[PV_QUAKE1] !=
				(byte *)akimbo_pair_geometry[hand] ||
			!V_AkimboEntityMatrixValid (&akimbo_pair_entities[hand],
				akimbo_pair_geometry[hand]))
			return false;
		else if (akimbo_pair_frozen)
		{
			if (akimbo_pair_entities[hand].frame != 0 ||
				akimbo_pair_entities[hand].lerp.prev_frame != 0 ||
				akimbo_pair_entities[hand].lerp.frame_change_time != 0.0 ||
				akimbo_pair_entities[hand].lerp.frame_duration != 0.0 ||
				akimbo_pair_entities[hand].lerp.snap_frames != 0)
				return false;
		}
		else if (akimbo_pair_entities[hand].frame != cl.viewent.frame)
			return false;
	return true;
}

void V_AkimboPairCollisionOffset (int physical_hand, vec3_t out_render_delta)
{
	if (!out_render_delta)
		return;
	VectorCopy (vec3_origin, out_render_delta);
	if (physical_hand < 0 || physical_hand > 1 || !V_AkimboPairReady ())
		return;
	VectorCopy (akimbo_pair_collision_offsets[physical_hand], out_render_delta);
}

int V_AkimboViewmodelHand (const entity_t *e)
{
	if (e == &akimbo_pair_entities[0])
		return 0;
	if (e == &akimbo_pair_entities[1])
		return 1;
	return -1;
}

entity_t *V_AkimboPairEntity (int physical_hand)
{
	return vulkan_globals.stereo_active && akimbo_pair_prepared &&
		physical_hand >= 0 && physical_hand < 2 ?
		&akimbo_pair_entities[physical_hand] : NULL;
}

qboolean V_AkimboTransformAnchor (int physical_hand,
	const vec3_t model_angles, vec3_t out_local)
{
	if (out_local)
		VectorCopy (vec3_origin, out_local);
	if (!out_local || !model_angles || physical_hand < 0 || physical_hand > 1 ||
		!V_AkimboPairReady ())
		return false;
	return V_AkimboTransformAnchorForPair (physical_hand, model_angles,
		akimbo_source_recipe, akimbo_source_geometry,
		&akimbo_pair_entities[physical_hand], akimbo_pair_geometry[physical_hand],
		out_local);
}

qboolean V_AkimboDwellEdgeOffsets (int physical_hand,
	const vec3_t model_angles, vec3_t out_base, vec3_t out_tip)
{
	if (out_base)
		VectorCopy (vec3_origin, out_base);
	if (out_tip)
		VectorCopy (vec3_origin, out_tip);
	if (!out_base || !out_tip || physical_hand < 0 || physical_hand > 1 ||
		!V_AkimboPairReady () || !V_AkimboRecipeIsDwellAxe (akimbo_source_recipe) ||
		!akimbo_pair_models[physical_hand] ||
		!akimbo_pair_models[physical_hand]->is_generated_akimbo_half)
		return false;
	return V_AkimboTransformDwellEdgesForPair (physical_hand, model_angles,
		&akimbo_pair_entities[physical_hand], akimbo_pair_geometry[physical_hand],
		out_base, out_tip);
}

void V_ClearWeaponCollisionPresentation (void)
{
	/* Remove a prior frame's applied offset before the next refdef/model pose. */
	if (tracked_weapon_collision_frame_valid)
		VectorSubtract (cl.viewent.origin, tracked_weapon_collision_offset,
			cl.viewent.origin);
	tracked_weapon_collision_frame_valid = false;
	VectorCopy (vec3_origin, tracked_weapon_collision_offset);
}

void V_PrepareWeaponCollisionPresentation (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const int dominant = VR_InputDominantPhysicalHand ();
	vec3_t torso_offset, torso, grip, muzzle, base, tip, hand_angles, delta;
	vec3_t axe_base, axe_tip;
	float head_height;
	entity_t *player;

	tracked_weapon_collision_frame_valid = false;
	VectorCopy (vec3_origin, tracked_weapon_collision_offset);
	if (VR_WeaponCalibrationAdjustActive () ||
		!VR_WeaponCollisionAuthorized () || !frame || !frame->focused ||
		!frame->should_render || cls.state != ca_connected || cls.signon != SIGNONS ||
		cls.demoplayback || key_dest != key_game || cl.intermission ||
		cl.stats[STAT_HEALTH] <= 0 || chase_active.value ||
		!cl.worldmodel || cl.worldmodel->needload || !cl.entities ||
		cl.viewentity <= 0 || cl.viewentity >= cl.num_entities ||
		!cl.viewent.model || cl.stats[STAT_WEAPON] <= 0 ||
		cl.stats[STAT_WEAPON] >= MAX_MODELS ||
		cl.viewent.model != cl.model_precache[cl.stats[STAT_WEAPON]] ||
		cl.viewent.model->needload || cl.viewent.model->type != mod_alias ||
		V_AkimboPairReady () || V_HeldMeleeEntity () ||
		!tracked_viewmodel_active || dominant < 0 || dominant > 1)
		return;

	for (int device = 0; device < 2; ++device)
	{
		const vrxr_device_t *tracked = &frame->devices[device == 0 ? 0 : dominant + 1];
		if (!tracked->valid || !tracked->tracked ||
			(device == 0 ? tracked->kind != VRXR_DEVICE_HEAD || tracked->hand != -1 :
			tracked->kind != VRXR_DEVICE_HAND || tracked->hand != dominant))
			return;
		for (int row = 0; row < 3; ++row)
			for (int column = 0; column < 4; ++column)
				if (!isfinite (tracked->matrix[row][column]))
					return;
	}

	if (!V_TrackedPresentationHandAngles (dominant, hand_angles) ||
		!VR_WeaponCalibrationCurrentMuzzle (muzzle) ||
		!VR_LocomotionMuzzleOffsetToWorld (muzzle, hand_angles,
			vr_gunmodelscale.value, vr_gunmodelpitch.value, dominant == 0, muzzle) ||
		!R_TrackedHeadBodyOffset (torso_offset) ||
		!R_TrackedHeadEyeHeight (cl.stats[STAT_VIEWHEIGHT], &head_height))
		return;

	player = &cl.entities[cl.viewentity];
	VectorAdd (player->origin, torso_offset, torso);
	torso[2] += head_height + view_stair_delta;
	VectorCopy (cl.viewent.origin, grip);
	if (VR_InputStockAxePresentationEdgeOffsets (dominant,
		axe_base, axe_tip))
	{
		VectorAdd (grip, axe_base, base);
		VectorAdd (grip, axe_tip, tip);
	}
	else
	{
		VectorCopy (grip, base);
		VectorAdd (grip, muzzle, tip);
	}
	tracked_weapon_collision_frame_valid = true;
	if (!CL_ResolveWeaponCollision (torso, grip, base, tip, delta))
		return; /* Keep the raw pose when the two-stage solve is unresolved. */

	VectorAdd (cl.viewent.origin, delta, cl.viewent.origin);
	VectorCopy (delta, tracked_weapon_collision_offset);
}

qboolean V_TrackedWeaponCollisionPresentation (vec3_t origin, vec3_t offset)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	if (origin)
		VectorCopy (vec3_origin, origin);
	if (offset)
		VectorCopy (vec3_origin, offset);
	if (!origin || !offset || !tracked_weapon_collision_frame_valid ||
		!VR_WeaponCollisionAuthorized () || !frame || !frame->focused ||
		!frame->should_render)
	{
		if (tracked_weapon_collision_frame_valid)
			V_ClearWeaponCollisionPresentation ();
		return false;
	}
	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite (cl.viewent.origin[axis]) ||
			!isfinite (tracked_weapon_collision_offset[axis]))
		{
			V_ClearWeaponCollisionPresentation ();
			return false;
		}
	VectorCopy (cl.viewent.origin, origin);
	VectorCopy (tracked_weapon_collision_offset, offset);
	return true;
}

qboolean V_TurnTrackedYaw (float delta)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	// Already received authoritative alignment takes priority. A later origin
	// rebase preserves this delta until the ordinary view resolver commits it.
	if (!frame || !frame->focused || !frame->devices[0].valid || !tracked_aim_ready ||
		tracked_reference_pending || tracked_server_yaw_pending || tracked_readback_yaw ||
		CL_AngleLocked () || cls.signon != SIGNONS || cls.demoplayback || cl.intermission ||
		!isfinite (delta) || !isfinite (tracked_local_yaw + delta) ||
		!isfinite (tracked_yaw + tracked_local_yaw + delta))
		return false;
	tracked_local_yaw += delta;
	// Retain the previous orientation: the next ordinary resolver must observe
	// this yaw delta, including in the inherited head/mouse and blended modes.
	return true;
}

qboolean V_UseTrackedView (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	return frame && frame->should_render && frame->devices[0].valid;
}

qboolean V_TrackedViewmodelActive (void)
{
	return tracked_viewmodel_active;
}

qboolean V_TrackedViewmodelShouldHide (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	return frame &&
		V_TrackedAimMode () == VR_AIMMODE_CONTROLLER && cls.signon == SIGNONS &&
		!cls.demoplayback && !cl.intermission && !con_forcedup &&
		!tracked_viewmodel_active;
}

float V_VRUnitsPerMetre (void)
{
	const float scale = vr_world_scale.value;
	const float units = scale / (1.5f * 0.0254f);
	return isfinite (units) && units > 0 ? units : 1.f / (1.5f * 0.0254f);
}

float V_VRFloorOffset (void)
{
	return isfinite (vr_floor_offset.value) ? vr_floor_offset.value : -16.f;
}

qboolean V_TrackedPlayerBase (float *viewheight)
{
	*viewheight = base_viewheight;
	return base_player_view && V_UseTrackedView ();
}

extern int in_forward, in_forward2, in_back;

vec3_t v_punchangles[2];	   // johnfitz -- copied from cl.punchangle.  0 is current, 1 is previous value. never the same unless map just loaded
double v_punchangles_times[2]; // spike -- times, to avoid assumptions...

extern qboolean needs_relink;

/*
===============
V_CalcRoll

Used by view and sv_user
===============
*/
float V_CalcRoll (vec3_t angles, vec3_t velocity)
{
	vec3_t forward, right, up;
	float  sign;
	float  side;
	float  value;

	AngleVectors (angles, forward, right, up);
	side = DotProduct (velocity, right);
	sign = side < 0 ? -1 : 1;
	side = fabs (side);

	value = V_UseTrackedView () ? 0 : cl_rollangle.value;
	//	if (cl.inwater)
	//		value *= 6;

	if (side < cl_rollspeed.value)
		side = side * value / cl_rollspeed.value;
	else
		side = value;

	return side * sign;
}

/*
===============
V_CalcBob

===============
*/
float V_CalcBob (void)
{
	float bob;
	float cycle;

	if (V_UseTrackedView () || !cl_bobcycle.value) /* Avoid divide-by-zero, don't bob */
		return 0.0f;

	cycle = cl.time - (int)(cl.time / cl_bobcycle.value) * cl_bobcycle.value;
	cycle /= cl_bobcycle.value;
	if (cycle < cl_bobup.value)
		cycle = M_PI * cycle / cl_bobup.value;
	else
		cycle = M_PI + M_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value);

	// bob is proportional to velocity in the xy plane
	// (don't count Z, or jumping messes it up)

	bob = sqrt (cl.velocity[0] * cl.velocity[0] + cl.velocity[1] * cl.velocity[1]) * cl_bob.value;
	// Con_Printf ("speed: %5.1f\n", VectorLength(cl.velocity));
	bob = bob * 0.3 + bob * 0.7 * sin (cycle);
	if (bob > 4)
		bob = 4;
	else if (bob < -7)
		bob = -7;
	return bob;
}

//=============================================================================

cvar_t v_centermove = {"v_centermove", "0.15", CVAR_NONE};
cvar_t v_centerspeed = {"v_centerspeed", "500", CVAR_NONE};

void V_StartPitchDrift (void)
{
	if (V_UseTrackedView ())
	{
		if (tracked_aim_ready && !cls.demoplayback && !cl.intermission && !CL_AngleLocked ())
		{
			cl.viewangles[PITCH] = tracked_view_angles[PITCH];
			cl.viewangles[YAW] = tracked_view_angles[YAW];
			VectorCopy (cl.viewangles, tracked_previous_aim);
			VectorCopy (vec3_origin, tracked_withheld_aim);
			tracked_controller_history_valid = false;
		}
		return;
	}
#if 1
	if (cl.laststop == cl.time)
	{
		return; // something else is keeping it from drifting
	}
#endif
	if (cl.nodrift || !cl.pitchvel)
	{
		cl.pitchvel = v_centerspeed.value;
		cl.nodrift = false;
		cl.driftmove = 0;
	}
}

void V_StopPitchDrift (void)
{
	cl.laststop = cl.time;
	cl.nodrift = true;
	cl.pitchvel = 0;
}

/*
===============
V_DriftPitch

Moves the client pitch angle towards cl.idealpitch sent by the server.

If the user is adjusting pitch manually, either with lookup/lookdown,
mlook and mouse, or klook and keyboard, pitch drifting is constantly stopped.

Drifting is enabled when the center view key is hit, mlook is released and
lookspring is non 0, or when
===============
*/
void V_DriftPitch (void)
{
	float delta, move;

	if (noclip_anglehack || !cl.onground || cls.demoplayback || CL_AngleLocked () || V_UseTrackedView ())
	// FIXME: noclip_anglehack is set on the server, so in a nonlocal game this won't work.
	{
		cl.driftmove = 0;
		cl.pitchvel = 0;
		return;
	}

	// don't count small mouse motion
	if (cl.nodrift)
	{
		if (fabs (cl.movecmds[(cl.movemessages - 1) & MOVECMDS_MASK].forwardmove) < cl_forwardspeed.value)
			cl.driftmove = 0;
		else
			cl.driftmove += host_frametime;

		if (cl.driftmove > v_centermove.value)
		{
			if (lookspring.value)
				V_StartPitchDrift ();
		}
		return;
	}

	if (v_autopitch.value)
		delta = cl.statsf[STAT_IDEALPITCH] - cl.viewangles[PITCH];
	else
		delta = -cl.viewangles[PITCH];

	if (!delta)
	{
		cl.pitchvel = 0;
		return;
	}

	move = host_frametime * cl.pitchvel;
	cl.pitchvel += host_frametime * v_centerspeed.value;

	// Con_Printf ("move: %f (%f)\n", move, host_frametime);

	if (delta > 0)
	{
		if (move > delta)
		{
			cl.pitchvel = 0;
			move = delta;
		}
		cl.viewangles[PITCH] += move;
	}
	else if (delta < 0)
	{
		if (move > -delta)
		{
			cl.pitchvel = 0;
			move = -delta;
		}
		cl.viewangles[PITCH] -= move;
	}
}

/*
==============================================================================

	VIEW BLENDING

==============================================================================
*/

const cshift_t cshift_water = {{130, 80, 50}, 128};
const cshift_t cshift_slime = {{0, 25, 5}, 150};
const cshift_t cshift_lava = {{255, 80, 0}, 150};

uint8_t v_blend[4]; // rgba 0 - 255

// johnfitz -- deleted BuildGammaTable(), V_CheckGamma(), gammatable[], and ramps[][]

/*
===============
V_ResetBlend
===============
*/
void V_ResetBlend (void)
{
	memset (&cl.cshift_empty, 0, sizeof (cl.cshift_empty));
	memset (cl.cshifts, 0, sizeof (cl.cshifts));
	cl.v_dmg_time = cl.v_dmg_roll = cl.v_dmg_pitch = 0.f;
}

/*
===============
V_ParseDamage
===============
*/
void V_ParseDamage (void)
{
	int		  armor, blood;
	vec3_t	  from;
	int		  i;
	vec3_t	  forward, right, up;
	entity_t *ent;
	float	  side;
	float	  count;

	armor = MSG_ReadByte ();
	blood = MSG_ReadByte ();
	for (i = 0; i < 3; i++)
		from[i] = MSG_ReadCoord (cl.protocolflags);

	count = blood * 0.5 + armor * 0.5;
	if (count < 10)
		count = 10;

	cl.faceanimtime = cl.time + 0.2; // but sbar face into pain frame

	if (cls.demoseeking)
		return;

	cl.cshifts[CSHIFT_DAMAGE].percent += 3 * count;
	if (cl.cshifts[CSHIFT_DAMAGE].percent < 0)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;
	if (cl.cshifts[CSHIFT_DAMAGE].percent > 150)
		cl.cshifts[CSHIFT_DAMAGE].percent = 150;

	if (armor > blood)
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 100;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100;
	}
	else if (armor)
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 220;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 50;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 50;
	}
	else
	{
		cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 255;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 0;
		cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 0;
	}

	//
	// calculate view angle kicks
	//
	if (V_UseTrackedView () && !vr_viewkick.value)
		return; // Preserve damage color/face feedback above.
	ent = &cl.entities[cl.viewentity];

	VectorSubtract (from, ent->origin, from);
	VectorNormalize (from);

	AngleVectors (ent->angles, forward, right, up);

	side = DotProduct (from, right);
	cl.v_dmg_roll = count * side * v_kickroll.value;

	side = DotProduct (from, forward);
	cl.v_dmg_pitch = count * side * v_kickpitch.value;

	cl.v_dmg_time = v_kicktime.value;
}

/*
==================
V_cshift_f
==================
*/
void V_cshift_f (void)
{
	cl.cshift_empty.destcolor[0] = atoi (Cmd_Argv (1));
	cl.cshift_empty.destcolor[1] = atoi (Cmd_Argv (2));
	cl.cshift_empty.destcolor[2] = atoi (Cmd_Argv (3));
	cl.cshift_empty.percent = atoi (Cmd_Argv (4));
}

/*
==================
V_BonusFlash_f

When you run over an item, the server sends this command
==================
*/
void V_BonusFlash_f (void)
{
	cl.cshifts[CSHIFT_BONUS].destcolor[0] = 215;
	cl.cshifts[CSHIFT_BONUS].destcolor[1] = 186;
	cl.cshifts[CSHIFT_BONUS].destcolor[2] = 69;
	cl.cshifts[CSHIFT_BONUS].percent = 50;
}

/*
=============
V_SetContentsColor

Underwater, lava, etc each has a color shift
=============
*/
void V_SetContentsColor (int contents)
{
	switch (contents)
	{
	case CONTENTS_EMPTY:
	case CONTENTS_SOLID:
	case CONTENTS_SKY:								   // johnfitz -- no blend in sky
		cl.cshifts[CSHIFT_CONTENTS] = cl.cshift_empty; // modifiable by server using v_cshift command
		break;
	case CONTENTS_LAVA:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
		break;
	case CONTENTS_SLIME:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
		break;
	default:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
	}
}

/*
=============
V_CalcPowerupCshift
=============
*/
void V_CalcPowerupCshift (void)
{
	if (cl.items & IT_QUAD)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	}
	else if (cl.items & IT_SUIT)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 20;
	}
	else if (cl.items & IT_INVISIBILITY)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 100;
		cl.cshifts[CSHIFT_POWERUP].percent = 100;
	}
	else if (cl.items & IT_INVULNERABILITY)
	{
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	}
	else
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
}

/*
=============
V_CalcBlend
=============
*/
void V_CalcBlend (void)
{
	float	r, g, b, a, a2;
	int		j;
	cvar_t *cshiftpercent_cvars[NUM_CSHIFTS] = {&gl_cshiftpercent_contents, &gl_cshiftpercent_damage, &gl_cshiftpercent_bonus, &gl_cshiftpercent_powerup};

	r = 0;
	g = 0;
	b = 0;
	a = 0;

	for (j = 0; j < NUM_CSHIFTS; j++)
	{
		if (!gl_cshiftpercent.value)
			continue;

		// johnfitz -- only apply leaf contents color shifts during intermission
		if (cl.intermission && j != CSHIFT_CONTENTS)
			continue;
		// johnfitz

		a2 = ((cl.cshifts[j].percent * gl_cshiftpercent.value) / 100.0) / 255.0;
		// QuakeSpasm -- also scale by the specific gl_cshiftpercent_* cvar
		a2 *= (cshiftpercent_cvars[j]->value / 100.0);
		// QuakeSpasm
		if (!a2)
			continue;
		a = a + a2 * (1 - a);
		a2 = a2 / a;
		r = r * (1 - a2) + cl.cshifts[j].destcolor[0] * a2;
		g = g * (1 - a2) + cl.cshifts[j].destcolor[1] * a2;
		b = b * (1 - a2) + cl.cshifts[j].destcolor[2] * a2;
	}

	v_blend[0] = CLAMP (0.0f, r, 255.0f);
	v_blend[1] = CLAMP (0.0f, g, 255.0f);
	v_blend[2] = CLAMP (0.0f, b, 255.0f);
	v_blend[3] = CLAMP (0.0f, a * 255.0f, 255.0f);
}

/*
=============
V_UpdateBlend -- johnfitz -- V_UpdatePalette cleaned up and renamed
=============
*/
static void V_UpdateBlend (void)
{
	int		 i, j;
	qboolean blend_changed;

	V_CalcPowerupCshift ();

	blend_changed = false;

	for (i = 0; i < NUM_CSHIFTS; i++)
	{
		if (cl.cshifts[i].percent != cl.prev_cshifts[i].percent)
		{
			blend_changed = true;
			cl.prev_cshifts[i].percent = cl.cshifts[i].percent;
		}
		for (j = 0; j < 3; j++)
			if (cl.cshifts[i].destcolor[j] != cl.prev_cshifts[i].destcolor[j])
			{
				blend_changed = true;
				cl.prev_cshifts[i].destcolor[j] = cl.cshifts[i].destcolor[j];
			}
	}

	// drop the damage value
	cl.cshifts[CSHIFT_DAMAGE].percent -= host_frametime * 150;
	if (cl.cshifts[CSHIFT_DAMAGE].percent <= 0)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;

	// drop the bonus value
	cl.cshifts[CSHIFT_BONUS].percent -= host_frametime * 100;
	if (cl.cshifts[CSHIFT_BONUS].percent <= 0)
		cl.cshifts[CSHIFT_BONUS].percent = 0;

	if (blend_changed)
		V_CalcBlend ();
}

/*
==============================================================================

	VIEW RENDERING

==============================================================================
*/

float angledelta (float a)
{
	a = anglemod (a);
	if (a > 180)
		a -= 360;
	return a;
}

/*
==================
CalcGunAngle
==================
*/
void CalcGunAngle (void)
{
	float		 yaw, pitch, move;
	static float oldyaw = 0;
	static float oldpitch = 0;

	yaw = r_refdef.viewangles[YAW];
	pitch = -r_refdef.viewangles[PITCH];

	yaw = angledelta (yaw - r_refdef.viewangles[YAW]) * 0.4;
	if (yaw > 10)
		yaw = 10;
	if (yaw < -10)
		yaw = -10;
	pitch = angledelta (-pitch - r_refdef.viewangles[PITCH]) * 0.4;
	if (pitch > 10)
		pitch = 10;
	if (pitch < -10)
		pitch = -10;
	move = host_frametime * 20;
	if (yaw > oldyaw)
	{
		if (oldyaw + move < yaw)
			yaw = oldyaw + move;
	}
	else
	{
		if (oldyaw - move > yaw)
			yaw = oldyaw - move;
	}

	if (pitch > oldpitch)
	{
		if (oldpitch + move < pitch)
			pitch = oldpitch + move;
	}
	else
	{
		if (oldpitch - move > pitch)
			pitch = oldpitch - move;
	}

	oldyaw = yaw;
	oldpitch = pitch;

	cl.viewent.angles[YAW] = r_refdef.viewangles[YAW] + yaw;
	cl.viewent.angles[PITCH] = -(r_refdef.viewangles[PITCH] + pitch);

	cl.viewent.angles[ROLL] -= v_idlescale.value * sin (cl.time * v_iroll_cycle.value) * v_iroll_level.value;
	cl.viewent.angles[PITCH] -= v_idlescale.value * sin (cl.time * v_ipitch_cycle.value) * v_ipitch_level.value;
	cl.viewent.angles[YAW] -= v_idlescale.value * sin (cl.time * v_iyaw_cycle.value) * v_iyaw_level.value;
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets (void)
{
	entity_t *ent;

	ent = &cl.entities[cl.viewentity];

	// absolutely bound refresh reletive to entity clipping hull
	// so the view can never be inside a solid wall

	if (r_refdef.vieworg[0] < ent->origin[0] - 14)
		r_refdef.vieworg[0] = ent->origin[0] - 14;
	else if (r_refdef.vieworg[0] > ent->origin[0] + 14)
		r_refdef.vieworg[0] = ent->origin[0] + 14;
	if (r_refdef.vieworg[1] < ent->origin[1] - 14)
		r_refdef.vieworg[1] = ent->origin[1] - 14;
	else if (r_refdef.vieworg[1] > ent->origin[1] + 14)
		r_refdef.vieworg[1] = ent->origin[1] + 14;
	if (r_refdef.vieworg[2] < ent->origin[2] - 22)
		r_refdef.vieworg[2] = ent->origin[2] - 22;
	else if (r_refdef.vieworg[2] > ent->origin[2] + 30)
		r_refdef.vieworg[2] = ent->origin[2] + 30;
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle (void)
{
	r_refdef.viewangles[ROLL] += v_idlescale.value * sin (cl.time * v_iroll_cycle.value) * v_iroll_level.value;
	r_refdef.viewangles[PITCH] += v_idlescale.value * sin (cl.time * v_ipitch_cycle.value) * v_ipitch_level.value;
	r_refdef.viewangles[YAW] += v_idlescale.value * sin (cl.time * v_iyaw_cycle.value) * v_iyaw_level.value;
}

/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll (void)
{
	float side;

	side = V_CalcRoll (cl.entities[cl.viewentity].angles, cl.velocity);
	r_refdef.viewangles[ROLL] += side;

	if (cl.v_dmg_time > 0)
	{
		if (!V_UseTrackedView () || vr_viewkick.value)
		{
			r_refdef.viewangles[ROLL] += cl.v_dmg_time / v_kicktime.value * cl.v_dmg_roll;
			r_refdef.viewangles[PITCH] += cl.v_dmg_time / v_kicktime.value * cl.v_dmg_pitch;
		}
		cl.v_dmg_time -= host_frametime;
	}

	if (cl.stats[STAT_HEALTH] <= 0 && !V_UseTrackedView ())
	{
		r_refdef.viewangles[ROLL] = 80; // dead view angle
		return;
	}
}

/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef (void)
{
	entity_t *ent, *view;
	float	  old;
	base_player_view = base_angles_valid = false;

	// ent is the player model (visible when out of body)
	ent = &cl.entities[cl.viewentity];
	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	VectorCopy (ent->origin, r_refdef.vieworg);
	VectorCopy (ent->angles, r_refdef.viewangles);
	if (V_UseTrackedView ())
	{
		r_refdef.viewangles[PITCH] = 0;
		r_refdef.viewangles[ROLL] = 0;
	}
	view->model = NULL;
	InvalidateTraceLineCache ();

	// allways idle in intermission
	old = v_idlescale.value;
	v_idlescale.value = 1;
	V_AddIdle ();
	v_idlescale.value = old;
}

/*
==================
V_CalcRefdef
==================
*/
void V_CalcRefdef (void)
{
	entity_t	 *ent, *view;
	int			  i;
	vec3_t		  forward, right, up;
	vec3_t		  angles;
	float		  bob;
	static float  oldz = 0;
	static vec3_t punch = {0, 0, 0}; // johnfitz -- v_gunkick
	float		  delta;			 // johnfitz -- v_gunkick

	V_DriftPitch ();

	// ent is the player model (visible when out of body)
	ent = &cl.entities[cl.viewentity];
	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	// transform the view offset by the model's matrix to get the offset from
	// model origin for the view
	ent->angles[YAW] = cl.viewangles[YAW];		// the model should face the view dir
	ent->angles[PITCH] = -cl.viewangles[PITCH]; // the model should face the view dir

	bob = V_CalcBob ();

	// refresh position
	VectorCopy (ent->origin, r_refdef.vieworg);
	base_player_view = base_angles_valid = true;
	base_viewheight = cl.stats[STAT_VIEWHEIGHT];
	VectorCopy (cl.viewangles, base_aim_angles);
	r_refdef.vieworg[2] += base_viewheight + bob;

	// never let it sit exactly on a node line, because a water plane can
	// dissapear when viewed with the eye exactly on it.
	// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
	r_refdef.vieworg[0] += 1.0 / 32;
	r_refdef.vieworg[1] += 1.0 / 32;
	r_refdef.vieworg[2] += 1.0 / 32;

	VectorCopy (cl.viewangles, r_refdef.viewangles);
	V_CalcViewRoll ();
	V_AddIdle ();

	// offsets
	angles[PITCH] = -ent->angles[PITCH]; // because entity pitches are actually backward
	angles[YAW] = ent->angles[YAW];
	angles[ROLL] = ent->angles[ROLL];

	AngleVectors (angles, forward, right, up);

	if (cl.maxclients <= 1) // johnfitz -- moved cheat-protection here from V_RenderView
		for (i = 0; i < 3; i++)
			r_refdef.vieworg[i] += scr_ofsx.value * forward[i] + scr_ofsy.value * right[i] + scr_ofsz.value * up[i];

	if (!V_UseTrackedView ())
		V_BoundOffsets ();

	// set up gun position
	VectorCopy (cl.viewangles, view->angles);

	CalcGunAngle ();

	VectorCopy (ent->origin, view->origin);
	view->origin[2] += cl.stats[STAT_VIEWHEIGHT];

	for (i = 0; i < 3; i++)
		view->origin[i] += forward[i] * bob * 0.4;
	view->origin[2] += bob;

	// johnfitz -- removed all gun position fudging code (was used to keep gun from getting covered by sbar)
	// MarkV -- restored this with r_viewmodel_quake cvar
	if (r_viewmodel_quake.value)
	{
		if (scr_viewsize.value == 110)
			view->origin[2] += 1;
		else if (scr_viewsize.value == 100)
			view->origin[2] += 2;
		else if (scr_viewsize.value == 90)
			view->origin[2] += 1;
		else if (scr_viewsize.value == 80)
			view->origin[2] += 0.5;
	}

	view->lerp.frame_finish_time = ent->lerp.frame_finish_time;

	// the weapon's frames come from stats, so its change detection lives here
	// ericw -- model check is done after the upper 8 bits of cl.stats[STAT_WEAPON] are filled in (broke on large maps like zendar.bsp)
	if (view->model != cl.model_precache[cl.stats[STAT_WEAPON]])
	{
		// don't lerp animation across model changes
		view->frame = cl.stats[STAT_WEAPONFRAME];
		view->lerp.prev_frame = view->frame;
		view->lerp.frame_change_time = 0;
		view->lerp.snap_frames = 0;
	}
	else if (view->frame != cl.stats[STAT_WEAPONFRAME])
	{
		if (view->lerp.snap_frames > 0)
		{
			view->lerp.snap_frames--;
			view->lerp.prev_frame = cl.stats[STAT_WEAPONFRAME];
		}
		else
			view->lerp.prev_frame = view->frame;
		view->frame = cl.stats[STAT_WEAPONFRAME];
		view->lerp.frame_change_time = cl.mtime[0];
		view->lerp.frame_duration = (view->lerp.frame_finish_time > cl.mtime[0]) ? view->lerp.frame_finish_time - cl.mtime[0] : 0.1;
	}

	view->model = cl.model_precache[cl.stats[STAT_WEAPON]];
	view->netstate.colormap = 0;

	// johnfitz -- v_gunkick
	if (v_gunkick.value == 1 && (!V_UseTrackedView () || vr_viewkick.value)) // original quake kick
		VectorAdd (r_refdef.viewangles, cl.punchangle, r_refdef.viewangles);
	if (v_gunkick.value == 2) // lerped kick
	{
		for (i = 0; i < 3; i++)
			if (punch[i] != v_punchangles[0][i])
			{
				double interval = v_punchangles_times[0] - v_punchangles_times[1];
				if (interval > 0.1)
					interval = 0.1;

				// speed determined by how far we need to lerp in 1/10th of a second
				delta = (v_punchangles[0][i] - v_punchangles[1][i]) * host_frametime / interval;

				if (delta > 0)
					punch[i] = q_min (punch[i] + delta, v_punchangles[0][i]);
				else if (delta < 0)
					punch[i] = q_max (punch[i] + delta, v_punchangles[0][i]);
			}

		// Keep interpolation current while suppressed so re-enabling kicks or
		// returning to desktop cannot replay a stale recoil accumulator.
		if (!V_UseTrackedView () || vr_viewkick.value)
			VectorAdd (r_refdef.viewangles, punch, r_refdef.viewangles);
	}
	// johnfitz

	// smooth out stair step ups
	if (!noclip_anglehack && cl.onground && ent->origin[2] - oldz > 0) // johnfitz -- added exception for noclip
	// FIXME: noclip_anglehack is set on the server, so in a nonlocal game this won't work.
	{
		float steptime;

		steptime = cl.time - cl.oldtime;
		if (steptime < 0)
			// FIXME	I_Error ("steptime < 0");
			steptime = 0;

		oldz += steptime * 80;
		if (oldz > ent->origin[2])
			oldz = ent->origin[2];
		if (ent->origin[2] - oldz > 12)
			oldz = ent->origin[2] - 12;
		view_stair_delta = oldz - ent->origin[2];
		r_refdef.vieworg[2] += view_stair_delta;
		view->origin[2] += view_stair_delta;
	}
	else
	{
		oldz = ent->origin[2];
		view_stair_delta = 0;
	}

	if (chase_active.value)
	{
		Chase_UpdateForDrawing (); // johnfitz
		VectorCopy (V_TrackedViewAngles (), base_aim_angles);
		base_player_view = false; // collision-traced camera, not a player-eye base
	}
}

/*
==================
V_RestoreAngles

Resets the viewentity angles to the last values received from the server
(undoing the manual adjustments performed by V_CalcRefdef)
==================
*/
void V_RestoreAngles (void)
{
	entity_t *ent = &cl.entities[cl.viewentity];
	VectorCopy (ent->msg_angles[0], ent->angles);
}

/*
=============
V_SetupFrame
=============
*/
void V_SetupFrame (void)
{
	qboolean refdef_updated = false;
	qboolean restore_desktop_viewmodel;

	V_RemovePredictionViewOffset ();
	restore_desktop_viewmodel = cl.paused && tracked_viewmodel_pose_applied &&
		(!V_TrackedSessionActive () || V_TrackedAimMode () != VR_AIMMODE_CONTROLLER);
	V_ClearWeaponCollisionPresentation ();
	V_UpdateBlend ();
	if (con_forcedup)
		base_player_view = base_angles_valid = false;
	if (!con_forcedup)
	{
		if (cl.intermission)
		{
			V_CalcIntermissionRefdef ();
			refdef_updated = true;
		}
		else if (!cl.paused || (V_UseTrackedView () && !base_angles_valid) ||
			restore_desktop_viewmodel)
		{
			V_CalcRefdef ();
			refdef_updated = true;
		}
	}
	V_UpdateTrackedViewmodel (refdef_updated);
	V_ApplyPredictionViewOffset (!con_forcedup && !cl.intermission &&
		!cl.paused && !cls.demoplayback && cls.state == ca_connected &&
		cls.signon == SIGNONS && !chase_active.value && base_player_view);
}

/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
extern vrect_t scr_vrect;

void V_RenderView (
	qboolean use_tasks, task_handle_t begin_rendering_task, task_handle_t setup_frame_task, task_handle_t draw_done_task, task_handle_t draw_gui_task)
{
	if (con_forcedup)
	{
		R_ClearDebugEntityInfo ();
		render_warp = false;
		return;
	}

	if (needs_relink)
	{
		if (use_tasks)
			Sys_Error ("V_RenderView: entities needed relink in main draw");
		CL_RelinkEntities ();
	}

	R_RenderView (use_tasks, begin_rendering_task, setup_frame_task, draw_done_task, draw_gui_task);
	return;
}

/*
==============================================================================

	INIT

==============================================================================
*/

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	Cvar_RegisterVariable (&vr_world_scale);
	Cvar_RegisterVariable (&vr_floor_offset);
	Cvar_RegisterVariable (&vr_hud_scale);
	Cvar_RegisterVariable (&vr_eye_tracking);
	Cvar_RegisterVariable (&vr_foveation);
	Cvar_RegisterVariable (&vr_mirror);
	Cvar_RegisterVariable (&vr_hidden_area);
	Cvar_RegisterVariable (&vr_viewkick);
	Cvar_RegisterVariable (&vr_aimmode);
	Cvar_RegisterVariable (&vr_deadzone);
	Cvar_RegisterVariable (&vr_gunangle);
	Cvar_RegisterVariable (&vr_gunmodelpitch);
	Cvar_RegisterVariable (&vr_gunmodelscale);
	Cvar_RegisterVariable (&vr_gunmodely);
	Cvar_SetCallback (&vr_aimmode, V_TrackedAimModeChanged);
	Cvar_SetCallback (&vr_deadzone, V_TrackedDeadzoneChanged);
	Cmd_AddCommand ("v_cshift", V_cshift_f);
	Cmd_AddCommand ("bf", V_BonusFlash_f);
	Cmd_AddCommand ("centerview", V_StartPitchDrift);

	Cvar_RegisterVariable (&v_centermove);
	Cvar_RegisterVariable (&v_centerspeed);

	Cvar_RegisterVariable (&v_iyaw_cycle);
	Cvar_RegisterVariable (&v_iroll_cycle);
	Cvar_RegisterVariable (&v_ipitch_cycle);
	Cvar_RegisterVariable (&v_iyaw_level);
	Cvar_RegisterVariable (&v_iroll_level);
	Cvar_RegisterVariable (&v_ipitch_level);

	Cvar_RegisterVariable (&v_idlescale);
	Cvar_RegisterVariable (&crosshair);
	Cvar_RegisterVariable (&crosshair_def);
	Cvar_RegisterVariable (&crosshair_size);
	Cvar_RegisterVariable (&crosshair_color);
	Cvar_RegisterVariable (&crosshair_alpha);
	Cvar_RegisterVariable (&gl_cshiftpercent);
	Cvar_RegisterVariable (&gl_cshiftpercent_contents); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_damage);	// QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_bonus);	// QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_powerup);	// QuakeSpasm

	Cvar_RegisterVariable (&scr_ofsx);
	Cvar_RegisterVariable (&scr_ofsy);
	Cvar_RegisterVariable (&scr_ofsz);
	Cvar_RegisterVariable (&cl_rollspeed);
	Cvar_RegisterVariable (&cl_rollangle);
	Cvar_RegisterVariable (&cl_bob);
	Cvar_RegisterVariable (&cl_bobcycle);
	Cvar_RegisterVariable (&cl_bobup);

	Cvar_RegisterVariable (&v_kicktime);
	Cvar_RegisterVariable (&v_kickroll);
	Cvar_RegisterVariable (&v_kickpitch);
	Cvar_RegisterVariable (&v_gunkick); // johnfitz

	Cvar_RegisterVariable (&v_autopitch);

	Cvar_RegisterVariable (&r_viewmodel_quake); // MarkV
}
