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
cvar_t vr_eye_tracking = {"vr_eye_tracking", "0", CVAR_ARCHIVE};
cvar_t vr_foveation = {"vr_foveation", "0", CVAR_ARCHIVE};
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
static qboolean tracked_aim_ready;
/* View-owner lifetime for the body-relative eye. Command samples can stop
 * temporarily on focus loss without returning the eye to positional tracking. */
static qboolean tracked_body_anchor;
static qboolean tracked_viewmodel_active;
static qboolean tracked_viewmodel_pose_applied;
static float view_stair_delta;
static qboolean tracked_reference_pending, tracked_readback_yaw, tracked_server_yaw_pending;
static float tracked_server_yaw;
static qboolean tracked_server_yaw_from_setangle;

static int V_TrackedAimMode (void)
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
}

void V_ResetTrackedAim (void)
{
	tracked_local_yaw = 0;
	tracked_body_anchor = false;
	tracked_viewmodel_active = false;
	tracked_viewmodel_pose_applied = false;
	view_stair_delta = 0;
	VR_InputInvalidateMotion ();
	tracked_aim_ready = false;
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
	VectorCopy (aim, tracked_previous_aim);
}

const float *V_TrackedViewAngles (void)
{
	return tracked_aim_ready && V_UseTrackedView () && !cls.demoplayback && !cl.intermission ? tracked_view_angles : cl.viewangles;
}

qboolean V_ApplyTrackedView (vec3_t angles, float *tracking_yaw)
{
	if (!tracked_aim_ready || !base_angles_valid || !V_UseTrackedView () || cls.demoplayback || cl.intermission)
		return false;
	// Replace only the input-aim contribution of the prepared base. Its kick
	// and idle contributions survive; a paused base receives the latest head.
	for (int i = 0; i < 3; ++i)
		angles[i] += tracked_view_angles[i] - base_aim_angles[i];
	// Chase stores the visual input used by its collision-traced base, so
	// paused tracking refreshes orientation without discarding that result.
	*tracking_yaw = tracked_view_angles[YAW] - tracked_raw_angles[YAW];
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

qboolean V_TrackedMovementAngles (int mode, int physical_offhand, vec3_t angles)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	float tracking_yaw;
	if (!V_TrackedMappingYaw (&tracking_yaw))
		return false;
	if (mode == VR_MOVEMENT_MODE_FOLLOW_HEAD)
		return VR_AimPoseAngles (frame->devices[0].matrix, tracking_yaw, angles);
	if ((mode != VR_MOVEMENT_MODE_FOLLOW_HAND && mode != VR_MOVEMENT_MODE_RAW_INPUT) ||
		physical_offhand < 0 || physical_offhand > 1)
		return false;
	const vrxr_device_t *hand = &frame->devices[physical_offhand + 1];
	if (!hand->valid || hand->kind != VRXR_DEVICE_HAND || hand->hand != physical_offhand)
		return false;
	return VR_LocomotionHandAngles (hand->matrix, tracking_yaw,
		isfinite (vr_gunangle.value) ? vr_gunangle.value : 32.f, angles);
}

qboolean V_TrackedHandBodyOffset (int physical_hand, vec3_t out)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const vrxr_device_t *hand;
	float tracking_yaw, base_viewheight, head_eye_height;
	vec3_t head_position, hand_position;

	if (out)
		VectorCopy (vec3_origin, out);
	if (!out || physical_hand < 0 || physical_hand > 1 || !frame ||
		!frame->should_render || !frame->devices[0].valid ||
		!V_TrackedMappingYaw (&tracking_yaw) || !V_TrackedPlayerBase (&base_viewheight))
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
	return VR_LocomotionHandBodyOffset (head_position, hand_position, tracking_yaw,
		V_VRUnitsPerMetre (), head_eye_height, out);
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
		vec3_t body_offset, hand_angles, model_angles, origin;
		if (V_TrackedHandBodyOffset (dominant, body_offset) &&
			V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, dominant, hand_angles) &&
			VR_LocomotionHandRotToViewmodelAngles (hand_angles, model_angles,
			vr_gunmodelpitch.value))
		{
			entity_t *ent = &cl.entities[cl.viewentity];
			qboolean origin_valid = true;
			for (int i = 0; i < 3; ++i)
			{
				origin[i] = ent->origin[i] + body_offset[i];
				if (i == 2)
					origin[i] += view_stair_delta;
				if (!isfinite (origin[i]))
				{
					origin_valid = false;
					break;
				}
			}
			if (origin_valid)
			{
				VectorCopy (origin, cl.viewent.origin);
				VectorCopy (model_angles, cl.viewent.angles);
				tracked_viewmodel_active = true;
				tracked_viewmodel_pose_applied = true;
				return;
			}
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
		if (tracked_aim_ready && V_TrackedAimMode () != VR_AIMMODE_CONTROLLER && !cls.demoplayback && !cl.intermission && !CL_AngleLocked ())
		{
			cl.viewangles[PITCH] = tracked_view_angles[PITCH];
			cl.viewangles[YAW] = tracked_view_angles[YAW];
			VectorCopy (cl.viewangles, tracked_previous_aim);
			VectorCopy (vec3_origin, tracked_withheld_aim);
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
	const qboolean restore_desktop_viewmodel = cl.paused &&
		tracked_viewmodel_pose_applied && (!V_TrackedSessionActive () ||
		V_TrackedAimMode () != VR_AIMMODE_CONTROLLER);

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
	Cvar_RegisterVariable (&vr_viewkick);
	Cvar_RegisterVariable (&vr_aimmode);
	Cvar_RegisterVariable (&vr_deadzone);
	Cvar_RegisterVariable (&vr_gunangle);
	Cvar_RegisterVariable (&vr_gunmodelpitch);
	Cvar_RegisterVariable (&vr_gunmodelscale);
	Cvar_RegisterVariable (&vr_gunmodely);
	Cvar_SetCallback (&vr_aimmode, V_TrackedAimModeChanged);
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
