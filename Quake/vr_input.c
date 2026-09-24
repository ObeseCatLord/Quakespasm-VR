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

/*
 * Minimal native OpenXR button adapter. Mapping and neutral-gate behavior are
 * derived from the pinned Quake VR donor vr.c (1327f795) plus the existing
 * OpenXR migration bridge in vr.c at 3080841333fa94000df7e1fb9e549c7158685dd6.
 * The bridge composes Vive PAD / other profiles STICK into the donor's legacy
 * SteamVR touchpad button, and Index PAD into legacy Axis2. Preserve that
 * composition here rather than inferring bindings from physical control names.
 * Runtime action sampling remains exclusively owned by vr_openxr.cpp. This
 * file consumes one already-completed vrxr_frame_t and emits native Key_Event
 * edges only.
 */

#include "quakedef.h"
#include "menu.h"
#include "r_vrik.h"
#include "vr_aim.h"
#include "vr_fbt.h"
#include "vr_fbt_filter.h"
#include "vr_fbt_profile.h"
#include "vr_fbt_storage.h"
#include "vr_input.h"
#include "vr_locomotion.h"
#include "vr_weapon_calibration.h"
#include "view.h"
#include "world.h"

#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct
{
	qboolean owned[MAX_KEYS];
	qboolean wait_neutral;
	qboolean trigger_down;
	qboolean identity_valid;
	int menu_trigger_key;
	int	 role;
	int	 profile;
} vr_input_hand_state_t;

typedef struct
{
	keydest_t destination;
	enum m_state_e menu_state;
	qboolean binding_capture;
	qboolean input_grab;
} vr_input_context_t;

static cvar_t vr_lefthanded = {"vr_lefthanded", "0", CVAR_ARCHIVE};
cvar_t vr_haptic = {"vr_haptic", "1", CVAR_ARCHIVE};
static cvar_t vr_joystick_axis_deadzone = {"vr_joystick_axis_deadzone", "0.25", CVAR_ARCHIVE};
static cvar_t vr_joystick_axis_menu_deadzone_extra = {"vr_joystick_axis_menu_deadzone_extra", "0.25", CVAR_ARCHIVE};
static cvar_t vr_joystick_axis_exponent = {"vr_joystick_axis_exponent", "1", CVAR_ARCHIVE};
static cvar_t vr_joystick_deadzone_trunc = {"vr_joystick_deadzone_trunc", "1", CVAR_ARCHIVE};
static cvar_t vr_movement_mode = {"vr_movement_mode", "0", CVAR_ARCHIVE};
cvar_t vr_gorilla = {"vr_gorilla", "0", CVAR_ARCHIVE};
static cvar_t vr_movement_speed = {"vr_movement_speed", "1", CVAR_ARCHIVE};
static cvar_t vr_snap_turn = {"vr_snap_turn", "0", CVAR_ARCHIVE};
static cvar_t vr_180_snap_turn = {"vr_180_snap_turn", "1", CVAR_ARCHIVE};
static cvar_t vr_turn_speed = {"vr_turn_speed", "2", CVAR_ARCHIVE};
static cvar_t vr_joystick_yaw_multi = {"vr_joystick_yaw_multi", "1", CVAR_ARCHIVE};
static cvar_t vr_vrik = {"vr_vrik", "1", CVAR_ARCHIVE};
static cvar_t vr_immersive_melee = {"vr_immersive_melee", "1", CVAR_ARCHIVE};
cvar_t vr_fbt_enabled = {"vr_fbt_enabled", "0", CVAR_ARCHIVE};
cvar_t vr_weapon_collision = {"vr_weapon_collision", "0", CVAR_ARCHIVE};

qboolean VR_WeaponCollisionAuthorized (void)
{
	return cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		(cl.vr_weapon_contact_mode & VR_WEAPON_CONTACT_CAP_COLLISION) != 0 &&
		vr_weapon_collision.value != 0.0f;
}

static qboolean VR_InputMeleeAuthorized (void)
{
	return cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		(cl.vr_weapon_contact_mode & VR_WEAPON_CONTACT_CAP_MELEE) != 0 &&
		cl.vr_weapon_contact_profile == VR_WEAPON_CONTACT_PROFILE_STOCK &&
		vr_immersive_melee.value != 0.0f;
}

extern cvar_t vr_aimmode;

static vr_input_hand_state_t vr_input_hands[2];
static qboolean vr_input_emitted[MAX_KEYS];
/* A calibration commit consumes only the dominant trigger, through release.
 * Keep other buttons and movement under the normal input owner. */
static qboolean vr_input_adjust_trigger_suppressed;
static qboolean vr_input_context_valid;
static vr_input_context_t vr_input_context;
static unsigned int vr_input_reset_generation;
static unsigned int vr_input_dispatch_epoch;
static unsigned int vr_input_menu_panel_dispatch_epoch;
static unsigned int vr_input_commands_depth;
static qboolean vr_input_move_wait_neutral;
static qboolean vr_input_turn_wait_neutral;
static int vr_input_last_snap;
static qboolean vr_input_turn180_queued;
/* Explicit tracking-yaw rebases must break physical contact history. */
static qboolean vr_input_contact_discontinuity;
static qboolean vr_input_calibration_contact_adjusting;
static qboolean vr_input_gorilla_discontinuity = true;
static vec3_t vr_input_roomscale_last_position;
static qboolean vr_input_roomscale_position_valid;

static vr_fbt_manager_t vr_input_fbt_manager;
static qboolean vr_input_fbt_initialized;
static vr_fbt_profile_t vr_input_fbt_profile;
static qboolean vr_input_fbt_profile_valid;
static uint64_t vr_input_fbt_snapshot_id;
static uint64_t vr_input_fbt_last_seen_sample_id;
static uint64_t vr_input_fbt_next_ephemeral_identity = 1;
static double vr_input_fbt_snapshot_time;
static qboolean vr_input_fbt_have_snapshot_time;
typedef struct
{
	vr_fbt_filter_output_t output;
	uint64_t sample_id;
	double output_time;
	qboolean present;
} vr_input_fbt_cached_target_t;

static vr_fbt_filter_t vr_input_fbt_filter;
static vr_input_fbt_cached_target_t vr_input_fbt_cached_targets[VR_FBT_ROLE_COUNT];
static uint64_t vr_input_fbt_filter_last_sample_id;
static qboolean vr_input_fbt_sender_ready;
static vr_input_fbt_calibration_state_t vr_input_fbt_calibration_state;
static vr_fbt_profile_capture_t vr_input_fbt_capture;
static vr_fbt_profile_t vr_input_fbt_preview_profile;
static qboolean vr_input_fbt_preview_valid;
static char vr_input_fbt_calibration_name[VR_FBT_PROFILE_NAME_MAX];
static qboolean vr_input_fbt_menu_calibration_name;
static uint64_t vr_input_fbt_capture_last_sample_id;
static struct
{
	unsigned int role_mask;
	uint64_t sample_id;
	vec3_t tracker_tracking[VR_FBT_ROLE_COUNT];
} vr_input_fbt_visual_raw_snapshot;
static struct
{
	uint64_t sample_id;
	const qmodel_t *model;
	int modelindex;
	int weapon;
	int hand;
	int role;
	int profile;
	int device_hand;
	int skin;
	unsigned int reset_generation;
	const aliashdr_t *geometry;
	char model_name[MAX_QPATH];
	char serial[256];
} vr_input_pending_contact_identity;
static struct
{
	uint64_t sample_id;
	const qmodel_t *model;
	int modelindex;
	int weapon;
	unsigned int reset_generation;
	struct
	{
		int role;
		int profile;
		int device_hand;
		char serial[256];
	} hands[2];
	qboolean valid;
} vr_input_pending_akimbo_identity;
static void VR_InputFBTPrepareCalibrationVisualSnapshot (const vrxr_frame_t *frame);
static qboolean VR_InputFBTMapTrackingVector (const vec3_t tracking,
	float presentation_yaw, float body_yaw, vec3_t root);
static struct
{
	uint64_t identity;
	qboolean connected;
} vr_input_fbt_slots[VRXR_MAX_DEVICES];

#define VR_INPUT_WIRE_MIN (-32768.0f)
#define VR_INPUT_WIRE_MAX 32767.0f
/* Match the server's contact envelope; reject an over-limit sample, never clamp. */
#define VR_INPUT_CONTACT_SPEED_MAX 20.0f
/* Keep producer samples within PM_VR_ROOMSCALE_MAX_DELTA in pmove.c. */
#define VR_INPUT_ROOM_SCALE_MAX_DELTA_UNITS 16.0f

static void VR_InputFBTResetFilterState (void)
{
	VR_FBT_FilterInit (&vr_input_fbt_filter);
	memset (vr_input_fbt_cached_targets, 0, sizeof (vr_input_fbt_cached_targets));
	/* A profile/reference reset must not make the already-reconciled sample
	 * eligible for a second filter update. */
	vr_input_fbt_filter_last_sample_id = vr_input_fbt_last_seen_sample_id;
	vr_input_fbt_sender_ready = false;
}

static void VR_InputFBTCancelCalibration (void)
{
	memset (&vr_input_fbt_capture, 0, sizeof (vr_input_fbt_capture));
	memset (&vr_input_fbt_preview_profile, 0, sizeof (vr_input_fbt_preview_profile));
	vr_input_fbt_preview_valid = false;
	vr_input_fbt_calibration_state = VR_INPUT_FBT_CALIBRATION_IDLE;
	vr_input_fbt_calibration_name[0] = '\0';
	vr_input_fbt_menu_calibration_name = false;
	vr_input_fbt_capture_last_sample_id = 0;
	memset (&vr_input_fbt_visual_raw_snapshot, 0,
		sizeof (vr_input_fbt_visual_raw_snapshot));
}

static vr_input_context_t VR_InputCurrentContext (void)
{
	vr_input_context_t context;
	context.destination = key_dest;
	context.menu_state = key_dest == key_menu ? m_state : m_none;
	context.binding_capture = M_WaitingForKeyBinding ();
	context.input_grab = Key_InputGrabActive ();
	return context;
}

static qboolean VR_InputSameContext (const vr_input_context_t *a, const vr_input_context_t *b)
{
	return a->destination == b->destination &&
		a->menu_state == b->menu_state &&
		a->binding_capture == b->binding_capture &&
		a->input_grab == b->input_grab;
}

static qboolean VR_InputContextMatchesCurrent (const vr_input_context_t *expected)
{
	const vr_input_context_t current_context = VR_InputCurrentContext ();
	return VR_InputSameContext (expected, &current_context);
}

static float VR_InputFiniteCvar (const cvar_t *var, float fallback)
{
	return isfinite (var->value) ? var->value : fallback;
}

static float VR_InputDeadzone (float extra)
{
	float deadzone = VR_InputFiniteCvar (&vr_joystick_axis_deadzone, 0.25f);
	if (deadzone < 0.0f)
		deadzone = 0.0f;
	else if (deadzone > 1.0f)
		deadzone = 1.0f;

	if (!isfinite (extra) || extra < 0.0f)
		extra = 0.0f;
	if (extra > 1.0f)
		extra = 1.0f;
	return deadzone + extra;
}

static float VR_InputStrongestAxis (const vrxr_input_t *input, int axis)
{
	float best = 0.0f;
	const float stick = input->stick[axis];
	const float pad = input->pad[axis];

	if (isfinite (stick))
		best = stick;
	if (isfinite (pad) && fabsf (pad) > fabsf (best))
		best = pad;
	return best;
}

static qboolean VR_InputAxesFinite (const vrxr_input_t *input)
{
	return isfinite (input->stick[0]) && isfinite (input->stick[1]) &&
		   isfinite (input->pad[0]) && isfinite (input->pad[1]);
}

static float VR_InputFilteredAxis (const vrxr_input_t *input, int axis, float extra)
{
	const float raw = VR_InputStrongestAxis (input, axis);
	const float magnitude = fabsf (raw);
	const float deadzone = VR_InputDeadzone (extra);
	float		value;

	if (!isfinite (raw) || raw == 0.0f || magnitude < deadzone)
		return 0.0f;

	value = magnitude;
	if (VR_InputFiniteCvar (&vr_joystick_deadzone_trunc, 1.0f) == 0.0f)
	{
		const float base = VR_InputDeadzone (0.0f);
		const float denom = 1.0f - base;
		if (denom <= 0.0f)
			return 0.0f;
		value = (value - base) / denom;
	}

	{
		const float exponent = VR_InputFiniteCvar (&vr_joystick_axis_exponent, 1.0f);
		if (exponent >= 0.0f)
			value = powf (value, exponent);
	}

	if (!isfinite (value) || value <= 0.0f)
		return 0.0f;
	return raw < 0.0f ? -value : value;
}

static float VR_InputTriggerValue (const vrxr_input_t *input)
{
	if (input->pressed & VRXR_BUTTON_TRIGGER)
		return 1.0f;
	return isfinite (input->trigger) ? input->trigger : 0.0f;
}

static int VR_InputRoleForPhysicalHand (int physical_hand)
{
	const qboolean lefthanded = VR_InputFiniteCvar (&vr_lefthanded, 0.0f) != 0.0f;
	return lefthanded ? 1 - physical_hand : physical_hand;
}

static int VR_InputPhysicalHandForRole (int role)
{
	return VR_InputRoleForPhysicalHand (0) == role ? 0 : 1;
}

int VR_InputDominantPhysicalHand (void)
{
	return VR_InputPhysicalHandForRole (VR_INPUT_ROLE_RIGHT);
}

void VR_InputTriggerHaptic (int logical_role, float duration_seconds, float amplitude)
{
	if (!vr_haptic.value ||
		(logical_role != VR_INPUT_ROLE_LEFT && logical_role != VR_INPUT_ROLE_RIGHT) ||
		!GL_OpenXRFrame ())
		return;

	/* GL_OpenXRFrame gates desktop and unattached sessions. VRXR_Haptic also
	 * checks that the active OpenXR session is running and focused. */
	VRXR_Haptic (VR_InputPhysicalHandForRole (logical_role), duration_seconds, amplitude);
}

static int VR_InputMovementMode (void)
{
	const float value = VR_InputFiniteCvar (&vr_movement_mode, 0.f);
	return value >= VR_MOVEMENT_MODE_FOLLOW_HEAD && value <= VR_MOVEMENT_MODE_RAW_INPUT ?
		(int)value : VR_MOVEMENT_MODE_FOLLOW_HEAD;
}

static qboolean VR_InputControllerAim (void)
{
	return vr_aimmode.value == VR_AIMMODE_CONTROLLER;
}

static void VR_InputMotionSettingsChanged (cvar_t *var)
{
	(void)var;
	VR_InputInvalidateMotion ();
}

static const char *VR_InputFBTRoleName (vr_fbt_role_t role)
{
	switch (role)
	{
	case VR_FBT_ROLE_HIP: return "hip";
	case VR_FBT_ROLE_LEFT_FOOT: return "left_foot";
	case VR_FBT_ROLE_RIGHT_FOOT: return "right_foot";
	default: return "unknown";
	}
}

static qboolean VR_InputFBTParseRole (const char *text, vr_fbt_role_t *role)
{
	if (!text || !role)
		return false;
	if (!strcmp (text, "hip"))
		*role = VR_FBT_ROLE_HIP;
	else if (!strcmp (text, "left_foot"))
		*role = VR_FBT_ROLE_LEFT_FOOT;
	else if (!strcmp (text, "right_foot"))
		*role = VR_FBT_ROLE_RIGHT_FOOT;
	else
		return false;
	return true;
}

qboolean VR_InputFBTGetRoleStatus (vr_fbt_role_t role,
	vr_fbt_role_status_t *status)
{
	return vr_input_fbt_initialized && status &&
		VR_FBT_GetRoleStatus (&vr_input_fbt_manager, role, status);
}

static qboolean VR_InputFBTCandidateAssignedElsewhere (
	const vr_fbt_candidate_status_t *candidate, vr_fbt_role_t requested_role,
	const vr_fbt_role_status_t roles[VR_FBT_ROLE_COUNT])
{
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		const vr_fbt_role_status_t *assigned = &roles[role];
		if ((vr_fbt_role_t)role == requested_role)
			continue;
		if (assigned->identity_kind == VR_FBT_IDENTITY_SERIAL &&
			candidate->has_safe_serial &&
			VR_FBT_SerialIsSafe (assigned->serial) &&
			!strcmp (assigned->serial, candidate->serial))
			return true;
		if (assigned->identity_kind == VR_FBT_IDENTITY_EPHEMERAL &&
			!candidate->has_safe_serial && candidate->ephemeral_identity &&
			assigned->ephemeral_identity == candidate->ephemeral_identity)
			return true;
	}
	return false;
}

static qboolean VR_InputFBTCandidateIsEligible (
	const vr_fbt_candidate_status_t *candidate, vr_fbt_role_t requested_role,
	const vr_fbt_role_status_t roles[VR_FBT_ROLE_COUNT])
{
	if (!candidate->connected)
		return false;
	if (candidate->has_safe_serial)
	{
		if (candidate->serial_ambiguous ||
			!VR_FBT_SerialIsSafe (candidate->serial))
			return false;
	}
	else if (!candidate->ephemeral_identity || candidate->ephemeral_ambiguous)
		return false;
	return !VR_InputFBTCandidateAssignedElsewhere (candidate, requested_role,
		roles);
}

static qboolean VR_InputFBTCandidateMatchesRole (
	const vr_fbt_candidate_status_t *candidate,
	const vr_fbt_role_status_t *role)
{
	if (role->identity_kind == VR_FBT_IDENTITY_SERIAL)
		return candidate->has_safe_serial &&
			VR_FBT_SerialIsSafe (role->serial) &&
			!strcmp (candidate->serial, role->serial);
	if (role->identity_kind == VR_FBT_IDENTITY_EPHEMERAL)
		return !candidate->has_safe_serial && candidate->ephemeral_identity &&
			candidate->ephemeral_identity == role->ephemeral_identity;
	return false;
}

qboolean VR_InputFBTCycleRole (vr_fbt_role_t requested_role, int direction)
{
	vr_fbt_role_status_t roles[VR_FBT_ROLE_COUNT];
	unsigned int candidate_ordinals[VR_FBT_MAX_CANDIDATES];
	unsigned int candidate_devices[VR_FBT_MAX_CANDIDATES];
	unsigned int candidate_count, eligible_count = 0;
	int current_option = -1, target_option, option_count;
	if (!vr_input_fbt_initialized || requested_role < 0 ||
		requested_role >= VR_FBT_ROLE_COUNT || !direction)
		return false;
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		if (!VR_FBT_GetRoleStatus (&vr_input_fbt_manager,
			(vr_fbt_role_t)role, &roles[role]))
			return false;

	candidate_count = VR_FBT_GetCandidateCount (&vr_input_fbt_manager);
	if (candidate_count > VR_FBT_MAX_CANDIDATES)
		candidate_count = VR_FBT_MAX_CANDIDATES;
	for (unsigned int ordinal = 0; ordinal < candidate_count; ++ordinal)
	{
		vr_fbt_candidate_status_t candidate;
		unsigned int insertion;
		if (!VR_FBT_GetCandidate (&vr_input_fbt_manager, ordinal, &candidate) ||
			!VR_InputFBTCandidateIsEligible (&candidate, requested_role, roles))
			continue;
		insertion = eligible_count;
		while (insertion > 0 &&
			candidate.device_index < candidate_devices[insertion - 1])
		{
			candidate_ordinals[insertion] = candidate_ordinals[insertion - 1];
			candidate_devices[insertion] = candidate_devices[insertion - 1];
			--insertion;
		}
		candidate_ordinals[insertion] = ordinal;
		candidate_devices[insertion] = candidate.device_index;
		++eligible_count;
	}

	if (roles[requested_role].identity_kind == VR_FBT_IDENTITY_NONE)
		current_option = 0;
	else
	{
		for (unsigned int ordinal = 0; ordinal < eligible_count; ++ordinal)
		{
			vr_fbt_candidate_status_t candidate;
			if (VR_FBT_GetCandidate (&vr_input_fbt_manager,
				candidate_ordinals[ordinal], &candidate) &&
				VR_InputFBTCandidateMatchesRole (&candidate,
					&roles[requested_role]))
			{
				current_option = (int)ordinal + 1;
				break;
			}
		}
	}

	/* Option zero is always unassigned, including when no safe candidate exists. */
	option_count = (int)eligible_count + 1;
	if (current_option < 0)
		target_option = direction < 0 && eligible_count ?
			(int)eligible_count : 0;
	else
		target_option = (current_option +
			(direction < 0 ? option_count - 1 : 1)) % option_count;

	if (target_option == 0)
	{
		if (roles[requested_role].identity_kind == VR_FBT_IDENTITY_NONE ||
			!VR_FBT_UnassignRole (&vr_input_fbt_manager, requested_role))
			return false;
	}
	else if (!VR_FBT_AssignCandidate (&vr_input_fbt_manager, requested_role,
		candidate_ordinals[target_option - 1]))
		return false;

	VR_InputFBTResetFilterState ();
	VR_InputFBTCancelCalibration ();
	return true;
}

qboolean VR_InputFBTGetCalibrationStatus (
	vr_input_fbt_calibration_status_t *status)
{
	double now;
	vr_fbt_profile_capture_progress_t progress;
	if (!vr_input_fbt_initialized || !status)
		return false;
	memset (status, 0, sizeof (*status));
	status->state = vr_input_fbt_calibration_state;
	status->profile_valid = vr_input_fbt_profile_valid;
	if (vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_IDLE)
		q_strlcpy (status->profile_name, vr_input_fbt_calibration_name,
			sizeof (status->profile_name));
	else if (vr_input_fbt_profile_valid)
		q_strlcpy (status->profile_name, vr_input_fbt_profile.name,
			sizeof (status->profile_name));
	if (vr_input_fbt_capture.started)
	{
		now = Sys_DoubleTime ();
		if (!isfinite (now))
			now = vr_input_fbt_snapshot_time;
		VR_FBT_ProfileCaptureGetProgress (&vr_input_fbt_capture, now, &progress);
		status->required_role_mask = progress.required_role_mask;
		memcpy (status->accepted, progress.accepted, sizeof (status->accepted));
		memcpy (status->rejected, progress.rejected, sizeof (status->rejected));
		status->snapshot_rejected = progress.snapshot_rejected;
		status->elapsed_seconds = progress.elapsed_seconds;
	}
	return true;
}

/* A reference-space or input reset invalidates samples and session-only IDs.
 * Keep only explicit safe-serial bindings, which are independent of tracking
 * origin and can be reconciled against the next OpenXR snapshot. */
static void VR_InputFBTReset (void)
{
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX];
	qboolean bound[VR_FBT_ROLE_COUNT] = {false};

	memset (serials, 0, sizeof (serials));
	if (vr_input_fbt_initialized)
	{
		for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		{
			vr_fbt_role_status_t status;
			if (VR_FBT_GetRoleStatus (&vr_input_fbt_manager, (vr_fbt_role_t)role, &status) &&
				status.identity_kind == VR_FBT_IDENTITY_SERIAL &&
				VR_FBT_SerialIsSafe (status.serial))
			{
				memcpy (serials[role], status.serial, sizeof (serials[role]));
				bound[role] = true;
			}
		}
	}

	VR_FBT_Init (&vr_input_fbt_manager);
	vr_input_fbt_initialized = true;
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		if (bound[role])
			VR_FBT_BindSerial (&vr_input_fbt_manager, (vr_fbt_role_t)role,
				serials[role]);
	vr_input_fbt_snapshot_id = 0;
	vr_input_fbt_last_seen_sample_id = 0;
	vr_input_fbt_next_ephemeral_identity = 1;
	vr_input_fbt_snapshot_time = 0.0;
	vr_input_fbt_have_snapshot_time = false;
	memset (vr_input_fbt_slots, 0, sizeof (vr_input_fbt_slots));
	VR_InputFBTResetFilterState ();
	VR_InputFBTCancelCalibration ();
}

static void VR_InputFBTEnabledChanged (cvar_t *var)
{
	(void)var;
	VR_InputFBTReset ();
}

static qboolean VR_InputFBTApplyProfileBindings (const vr_fbt_profile_t *profile,
	vr_fbt_manager_t *staged)
{
	if (!profile || !staged || !vr_input_fbt_initialized)
		return false;

	/* Build the complete assignment on a copy. A rejected serial must leave the
	 * live roles and profile selection untouched. Manager roles are keyed only
	 * by safe serials; tracker indexes and enumeration order are never used. */
	*staged = vr_input_fbt_manager;
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		if (!VR_FBT_UnassignRole (staged, (vr_fbt_role_t)role))
			return false;

	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		const vr_fbt_profile_role_entry_t *entry = &profile->roles[role];
		if (entry->present &&
			(!VR_FBT_SerialIsSafe (entry->serial) ||
			 !VR_FBT_BindSerial (staged, (vr_fbt_role_t)role, entry->serial)))
			return false;
	}
	return true;
}

static qboolean VR_InputFBTSelectProfile (const vr_fbt_profile_t *profile,
	qboolean persist_selection)
{
	vr_fbt_manager_t staged;
	vr_fbt_storage_error_t storage_error = VR_FBT_STORAGE_OK;
	qboolean selection_not_durable = false;

	if (!profile || !vr_input_fbt_initialized ||
		!VR_FBT_StorageNameIsSafe (profile->name) ||
		profile->schema_version != VR_FBT_PROFILE_SCHEMA_VERSION ||
		profile->calibration_algorithm != VR_FBT_PROFILE_CALIBRATION_ALGORITHM ||
		strcmp (profile->avatar_fingerprint,
			VR_FBT_PROFILE_AVATAR_FINGERPRINT))
		return false;

	if (!VR_InputFBTApplyProfileBindings (profile, &staged))
	{
		Con_Printf ("FBT: profile bindings were rejected; current profile kept\n");
		return false;
	}

	/* Persist only after compatibility and every role binding have succeeded.
	 * A visible but non-durable replacement still selects this profile now. */
	if (persist_selection &&
		!VR_FBT_StorageSaveSelected (profile->name, &storage_error))
	{
		if (storage_error != VR_FBT_STORAGE_ERR_COMMITTED_NOT_DURABLE)
		{
			Con_Printf ("FBT: could not select profile (%d); current profile kept\n",
				(int)storage_error);
			return false;
		}
		selection_not_durable = true;
	}

	vr_input_fbt_manager = staged;
	vr_input_fbt_profile = *profile;
	vr_input_fbt_profile_valid = true;
	VR_InputFBTResetFilterState ();
	VR_InputFBTCancelCalibration ();
	if (selection_not_durable)
		Con_Warning ("FBT: selected profile is visible but not crash-durable\n");
	return true;
}

/* Save the profile before changing selected.cfg. A failed selected-name write
 * can leave a new profile file visible, but never changes the live selection. */
static qboolean VR_InputFBTSaveAndSelectProfile (const vr_fbt_profile_t *profile,
	vr_fbt_profile_error_t *profile_error,
	vr_fbt_storage_error_t *storage_error, qboolean *not_durable,
	qboolean *profile_visible)
{
	char selected[VR_FBT_PROFILE_NAME_MAX];
	vr_fbt_manager_t staged;
	qboolean already_selected = false;

	if (not_durable)
		*not_durable = false;
	if (profile_visible)
		*profile_visible = false;
	if (profile_error)
		*profile_error = VR_FBT_PROFILE_OK;
	if (storage_error)
		*storage_error = VR_FBT_STORAGE_OK;
	if (!profile || !profile_error || !storage_error || !not_durable ||
		!profile_visible || !VR_InputFBTApplyProfileBindings (profile, &staged))
		return false;
	if (VR_FBT_StorageLoadSelected (selected, sizeof (selected), storage_error))
		already_selected = !strcmp (selected, profile->name);
	if (!VR_FBT_StorageSaveProfile (profile, profile_error, storage_error))
	{
		if (*storage_error != VR_FBT_STORAGE_ERR_COMMITTED_NOT_DURABLE)
			return false;
		*not_durable = true;
	}
	*profile_visible = true;
	if (!already_selected &&
		!VR_FBT_StorageSaveSelected (profile->name, storage_error))
	{
		if (*storage_error != VR_FBT_STORAGE_ERR_COMMITTED_NOT_DURABLE)
			return false;
		*not_durable = true;
	}
	vr_input_fbt_manager = staged;
	vr_input_fbt_profile = *profile;
	vr_input_fbt_profile_valid = true;
	VR_InputFBTResetFilterState ();
	VR_InputFBTCancelCalibration ();
	return true;
}

static void VR_InputFBTProfileSave_f (void)
{
	vr_fbt_storage_error_t storage_error = VR_FBT_STORAGE_OK;
	vr_fbt_profile_error_t profile_error = VR_FBT_PROFILE_OK;
	qboolean not_durable, profile_visible;

	if (Cmd_Argc () != 1)
	{
		Con_Printf ("usage: vr_fbt_profile_save\n");
		return;
	}
	if (!vr_input_fbt_profile_valid)
	{
		Con_Printf ("FBT: no accepted profile to save; calibrate and accept one first\n");
		return;
	}
	if (!VR_InputFBTSaveAndSelectProfile (&vr_input_fbt_profile,
		&profile_error, &storage_error, &not_durable, &profile_visible))
	{
		Con_Printf ("FBT: profile save failed (%d); current profile kept%s\n",
			(int)storage_error, profile_visible ? "; saved file remains visible" : "");
		return;
	}
	if (not_durable)
		Con_Warning ("FBT: saved profile is visible but not crash-durable\n");
	Con_Printf ("FBT: saved accepted profile %s\n", vr_input_fbt_profile.name);
}

static void VR_InputFBTLoadSelectedProfile (void)
{
	char selected[VR_FBT_PROFILE_NAME_MAX];
	vr_fbt_profile_t loaded;
	vr_fbt_storage_error_t storage_error;
	vr_fbt_profile_error_t profile_error;

	if (!VR_FBT_StorageLoadSelected (selected, sizeof (selected), &storage_error))
		return;
	if (!VR_FBT_StorageLoadProfile (selected, &loaded, &profile_error,
		&storage_error))
	{
		Con_Printf ("FBT: saved profile %s could not be loaded (%d)\n",
			selected, (int)storage_error);
		return;
	}
	if (!VR_InputFBTSelectProfile (&loaded, false))
		Con_Printf ("FBT: saved profile %s is incompatible; current bindings kept\n",
			selected);
}

static void VR_InputFBTProfileSelect_f (void)
{
	vr_fbt_profile_t loaded;
	vr_fbt_storage_error_t storage_error;
	vr_fbt_profile_error_t profile_error;
	const char *name;

	if (Cmd_Argc () != 2 || !VR_FBT_StorageNameIsSafe (Cmd_Argv (1)))
	{
		Con_Printf ("usage: vr_fbt_profile_select <[A-Za-z0-9_-]{1,32}>\n");
		return;
	}
	name = Cmd_Argv (1);
	if (!VR_FBT_StorageLoadProfile (name, &loaded, &profile_error,
		&storage_error))
	{
		Con_Printf ("FBT: could not load profile %s (%d); current profile kept\n",
			name, (int)storage_error);
		return;
	}
	if (VR_InputFBTSelectProfile (&loaded, true))
		Con_Printf ("FBT: selected profile %s\n", loaded.name);
}

static void VR_InputFBTProfileReset_f (void)
{
	vr_fbt_manager_t staged;

	if (Cmd_Argc () != 1)
	{
		Con_Printf ("usage: vr_fbt_profile_reset\n");
		return;
	}
	if (!vr_input_fbt_initialized)
		return;
	staged = vr_input_fbt_manager;
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		VR_FBT_UnassignRole (&staged, (vr_fbt_role_t)role);
	vr_input_fbt_manager = staged;
	memset (&vr_input_fbt_profile, 0, sizeof (vr_input_fbt_profile));
	vr_input_fbt_profile_valid = false;
	VR_InputFBTResetFilterState ();
	VR_InputFBTCancelCalibration ();
	Con_Printf ("FBT: cleared runtime profile and bindings; saved files were not removed\n");
}

static void VR_InputFBTProfileList_f (void)
{
	/* Bound raw directory entries as well as the number of names sent to the
	 * console. Sys_FindFirst/Next use native OS iteration on each platform. */
	enum { ENTRY_LIMIT = 4096, OUTPUT_LIMIT = 128 };
	char directory[MAX_OSPATH];
	const char *write_root;
	findfile_t *find;
	int path_length;
	int count = 0;
	int examined = 0;
	qboolean truncated = false;

	if (Cmd_Argc () != 1)
	{
		Con_Printf ("usage: vr_fbt_profile_list\n");
		return;
	}

	if (vr_input_fbt_profile_valid)
	{
		unsigned int roles = 0;
		for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
			roles += vr_input_fbt_profile.roles[role].present != 0;
		Con_Printf ("FBT: selected profile %s (%u calibrated role%s)\n",
			vr_input_fbt_profile.name, roles, roles == 1 ? "" : "s");
	}
	else
		Con_Printf ("FBT: no selected calibration profile\n");

	write_root = COM_GetWriteRoot ();
	if (!write_root || !write_root[0])
	{
		Con_Printf ("FBT: could not enumerate saved profiles\n");
		return;
	}
	path_length = q_snprintf (directory, sizeof (directory), "%s/vrik/profiles",
		write_root);
	if (path_length < 0 || (size_t)path_length >= sizeof (directory))
	{
		Con_Printf ("FBT: could not enumerate saved profiles\n");
		return;
	}

	Con_Printf ("FBT: saved profiles:");
	find = Sys_FindFirst (directory, NULL);
	while (find && examined < ENTRY_LIMIT)
	{
		const size_t length = strlen (find->name);
		if (!(find->attribs & FA_DIRECTORY) && length > 4 &&
			!q_strcasecmp (find->name + length - 4, ".cfg") &&
			length - 4 < VR_FBT_PROFILE_NAME_MAX)
		{
			char name[VR_FBT_PROFILE_NAME_MAX];
			memcpy (name, find->name, length - 4);
			name[length - 4] = '\0';
			if (VR_FBT_StorageNameIsSafe (name))
			{
				if (count < OUTPUT_LIMIT)
					Con_Printf (" %s", name);
				else
					truncated = true;
				++count;
			}
		}
		++examined;
		find = Sys_FindNext (find);
	}
	if (find)
	{
		/* Sys_FindNext closes the handle at end-of-directory; close it here
		 * when the raw-entry limit stopped iteration early. */
		Sys_FindClose (find);
		truncated = true;
	}
	Con_Printf ("%s%s\n", count ? "" : " none",
		truncated ? " (listing truncated)" : "");
	Con_Printf ("FBT: use vr_fbt_profile_select <name> to load one\n");
}

static qboolean VR_InputFBTMatrixFinite (const float matrix[3][4])
{
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 4; ++column)
			if (!isfinite (matrix[row][column]))
				return false;
	return true;
}

static qboolean VR_InputFBTVectorFinite (const float vector[3])
{
	return isfinite (vector[0]) && isfinite (vector[1]) && isfinite (vector[2]);
}

static uint64_t VR_InputFBTNewEphemeralIdentity (void)
{
	uint64_t identity = vr_input_fbt_next_ephemeral_identity++;
	if (!identity)
		identity = vr_input_fbt_next_ephemeral_identity++;
	return identity;
}

static void VR_InputFBTCopySafeSerial (char destination[VR_FBT_SERIAL_MAX],
	const char source[256])
{
	size_t length = 0;
	memset (destination, 0, VR_FBT_SERIAL_MAX);
	while (length < 256 && source[length])
		++length;
	if (length == 0 || length >= VR_FBT_SERIAL_MAX)
		return;
	memcpy (destination, source, length);
	destination[length] = '\0';
	if (!VR_FBT_SerialIsSafe (destination))
		destination[0] = '\0';
}

/* Build candidates from one completed, immutable OpenXR frame. Device slots
 * 0..2 belong to the head and hands; tracker identity is never inferred from
 * slot order and remains session-scoped when no safe runtime serial exists. */
static void VR_InputFBTReconcile (const vrxr_frame_t *frame)
{
	vr_fbt_candidate_t candidates[VR_FBT_MAX_CANDIDATES];
	unsigned int candidate_count = 0;
	double snapshot_time;
	uint64_t snapshot_id;

	if (!vr_fbt_enabled.value || !frame || !vr_input_fbt_initialized ||
		!frame->sample_id || frame->sample_id == vr_input_fbt_last_seen_sample_id)
		return;

	/* sample_id, not this adapter call's timestamp or frame address, is the
	 * freshness boundary. The manager time is local monotonic observation time. */
	snapshot_time = Sys_DoubleTime ();
	if (!isfinite (snapshot_time))
		snapshot_time = vr_input_fbt_have_snapshot_time ? vr_input_fbt_snapshot_time : 0.0;
	if (vr_input_fbt_have_snapshot_time && snapshot_time < vr_input_fbt_snapshot_time)
		snapshot_time = vr_input_fbt_snapshot_time;
	snapshot_id = vr_input_fbt_snapshot_id + 1;
	if (!snapshot_id)
		snapshot_id = 1;

	for (unsigned int device_index = 3; device_index < VRXR_MAX_DEVICES; ++device_index)
	{
		const vrxr_device_t *device = &frame->devices[device_index];
		vr_fbt_candidate_t *candidate;
		qboolean connected;
		qboolean pose_valid;

		if (device->kind != VRXR_DEVICE_TRACKER)
		{
			vr_input_fbt_slots[device_index].identity = 0;
			vr_input_fbt_slots[device_index].connected = false;
			continue;
		}
		if (candidate_count >= VR_FBT_MAX_CANDIDATES)
			break;

		candidate = &candidates[candidate_count++];
		memset (candidate, 0, sizeof (*candidate));
		candidate->device_index = device_index;
		candidate->snapshot_id = snapshot_id;
		candidate->snapshot_time = snapshot_time;
		connected = device->connected != 0;
		pose_valid = connected && device->valid && device->tracked &&
			VR_InputFBTMatrixFinite (device->matrix);
		candidate->connected = connected;
		candidate->pose_valid = pose_valid;
		candidate->tracking_result = pose_valid ? VR_FBT_TRACKING_RESULT_RUNNING_OK : 0;

		VR_InputFBTCopySafeSerial (candidate->serial, device->serial);
		if (connected)
		{
			if (!vr_input_fbt_slots[device_index].connected ||
				!vr_input_fbt_slots[device_index].identity)
				vr_input_fbt_slots[device_index].identity = VR_InputFBTNewEphemeralIdentity ();
			vr_input_fbt_slots[device_index].connected = true;
			if (!candidate->serial[0])
				candidate->ephemeral_identity = vr_input_fbt_slots[device_index].identity;
		}
		else
		{
			vr_input_fbt_slots[device_index].identity = 0;
			vr_input_fbt_slots[device_index].connected = false;
		}

		if (pose_valid)
		{
			memcpy (candidate->device_to_absolute_tracking, device->matrix,
				sizeof (candidate->device_to_absolute_tracking));
			if (device->velocity_valid && VR_InputFBTVectorFinite (device->velocity))
				memcpy (candidate->velocity, device->velocity, sizeof (candidate->velocity));
			if (device->angular_velocity_valid && VR_InputFBTVectorFinite (device->angular_velocity))
				memcpy (candidate->angular_velocity, device->angular_velocity,
					sizeof (candidate->angular_velocity));
		}
	}

	if (VR_FBT_Reconcile (&vr_input_fbt_manager, snapshot_id, snapshot_time,
		candidates, candidate_count))
	{
		vr_input_fbt_snapshot_id = snapshot_id;
		vr_input_fbt_last_seen_sample_id = frame->sample_id;
		vr_input_fbt_snapshot_time = snapshot_time;
		vr_input_fbt_have_snapshot_time = true;
	}
}

static const char *VR_InputFBTStateName (vr_fbt_state_t state)
{
	switch (state)
	{
	case VR_FBT_STATE_UNASSIGNED: return "unassigned";
	case VR_FBT_STATE_CONNECTED_INVALID: return "connected-invalid";
	case VR_FBT_STATE_TRACKING: return "tracking";
	case VR_FBT_STATE_PREDICTING: return "predicting";
	case VR_FBT_STATE_LOST: return "lost";
	default: return "unknown";
	}
}

static void VR_InputFBTIdentityLabel (const vr_fbt_role_status_t *role,
	const vr_fbt_candidate_status_t *candidate, char *label, size_t label_size)
{
	const char *serial = role ? role->serial : candidate->serial;
	const qboolean safe = role ? role->identity_kind == VR_FBT_IDENTITY_SERIAL :
		candidate->has_safe_serial;
	uint32_t hash = 2166136261u;
	const unsigned char *cursor = (const unsigned char *)serial;

	if (!safe)
	{
		q_snprintf (label, label_size, "session-only");
		return;
	}
	while (*cursor)
	{
		hash ^= *cursor++;
		hash *= 16777619u;
	}
	q_snprintf (label, label_size, "serial#%08x", (unsigned int)hash);
}

static double VR_InputFBTAge (double then, double now)
{
	return isfinite (then) && isfinite (now) && then > 0.0 && now >= then ? now - then : 0.0;
}

static void VR_InputFBTList_f (void)
{
	double now;
	if (Cmd_Argc () != 1)
	{
		Con_Printf ("usage: vr_fbt_list\n");
		return;
	}
	now = Sys_DoubleTime ();
	if (!isfinite (now))
		now = vr_input_fbt_snapshot_time;
	Con_Printf ("FBT: %s, %u tracker candidate%s, snapshot %llu age %.3fs\n",
		vr_fbt_enabled.value ? "enabled" : "disabled",
		VR_FBT_GetCandidateCount (&vr_input_fbt_manager),
		VR_FBT_GetCandidateCount (&vr_input_fbt_manager) == 1 ? "" : "s",
		(unsigned long long)vr_input_fbt_manager.last_snapshot_id,
		VR_InputFBTAge (vr_input_fbt_manager.last_snapshot_time, now));
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		vr_fbt_role_status_t status;
		char identity[20];
		if (!VR_FBT_GetRoleStatus (&vr_input_fbt_manager, (vr_fbt_role_t)role, &status))
			continue;
		if (status.identity_kind == VR_FBT_IDENTITY_NONE)
		{
			Con_Printf ("  role %s: unassigned\n", VR_InputFBTRoleName ((vr_fbt_role_t)role));
			continue;
		}
		VR_InputFBTIdentityLabel (&status, NULL, identity, sizeof (identity));
		if (status.device_index == UINT_MAX)
			Con_Printf ("  role %s: %s offline %s tracking %d snapshot %llu age %.3fs\n",
				VR_InputFBTRoleName ((vr_fbt_role_t)role), VR_InputFBTStateName (status.state),
				identity, status.tracking_result,
				(unsigned long long)vr_input_fbt_manager.last_snapshot_id,
				VR_InputFBTAge (status.last_reconciled_time, now));
		else
			Con_Printf ("  role %s: %s index %u %s tracking %d snapshot %llu age %.3fs\n",
				VR_InputFBTRoleName ((vr_fbt_role_t)role), VR_InputFBTStateName (status.state),
				status.device_index, identity, status.tracking_result,
				(unsigned long long)vr_input_fbt_manager.last_snapshot_id,
				VR_InputFBTAge (status.last_reconciled_time, now));
	}
	for (unsigned int index = 0; index < VR_FBT_GetCandidateCount (&vr_input_fbt_manager); ++index)
	{
		vr_fbt_candidate_status_t status;
		char identity[20];
		const char *state;
		if (!VR_FBT_GetCandidate (&vr_input_fbt_manager, index, &status))
			continue;
		state = !status.connected ? "disconnected" : !status.pose_valid ? "connected-invalid" :
			status.tracking_result == VR_FBT_TRACKING_RESULT_RUNNING_OK ? "tracking" : "predicting";
		VR_InputFBTIdentityLabel (NULL, &status, identity, sizeof (identity));
		Con_Printf ("  candidate %u: %s index %u %s tracking %d snapshot %llu age %.3fs\n",
			index, state, status.device_index, identity, status.tracking_result,
			(unsigned long long)status.snapshot_id,
			VR_InputFBTAge (status.snapshot_time, now));
	}
}

static qboolean VR_InputFBTParseDecimal (const char *text, unsigned int *value)
{
	unsigned int parsed = 0;
	const unsigned char *cursor = (const unsigned char *)text;
	if (!text || !*text || !value)
		return false;
	while (*cursor)
	{
		unsigned int digit;
		if (*cursor < '0' || *cursor > '9')
			return false;
		digit = *cursor++ - '0';
		if (parsed > (UINT_MAX - digit) / 10)
			return false;
		parsed = parsed * 10 + digit;
	}
	*value = parsed;
	return true;
}

static int VR_InputFBTFindConnectedDevice (unsigned int device_index)
{
	for (unsigned int index = 0; index < VR_FBT_GetCandidateCount (&vr_input_fbt_manager); ++index)
	{
		vr_fbt_candidate_status_t status;
		if (VR_FBT_GetCandidate (&vr_input_fbt_manager, index, &status) &&
			status.device_index == device_index && status.connected)
			return (int)index;
	}
	return -1;
}

static void VR_InputFBTAssign_f (void)
{
	const char *selector;
	const char *serial = NULL;
	vr_fbt_role_t role;
	unsigned int device_index;
	int candidate = -1;
	qboolean explicit_index = false;

	if (Cmd_Argc () != 3 || !VR_InputFBTParseRole (Cmd_Argv (2), &role))
	{
		Con_Printf ("usage: vr_fbt_assign <index:<decimal>|serial:<safe>|device-index|safe-serial> <hip|left_foot|right_foot>\n");
		return;
	}
	selector = Cmd_Argv (1);
	if (!strncmp (selector, "index:", 6))
	{
		explicit_index = true;
		selector += 6;
	}
	else if (!strncmp (selector, "serial:", 7))
		serial = selector + 7;

	if (!serial && VR_InputFBTParseDecimal (selector, &device_index))
	{
		candidate = VR_InputFBTFindConnectedDevice (device_index);
		if (candidate >= 0)
		{
			if (!VR_FBT_AssignCandidate (&vr_input_fbt_manager, role, (unsigned int)candidate))
			{
				Con_Printf ("FBT: tracker is ambiguous or already assigned to another role\n");
				return;
			}
		}
		else if (explicit_index)
		{
			Con_Printf ("FBT: device index %u is not a connected tracker candidate\n", device_index);
			return;
		}
		else
			serial = selector;
	}
	else if (!serial)
	{
		if (explicit_index)
		{
			Con_Printf ("FBT: index selector requires a full decimal device index\n");
			return;
		}
		serial = selector;
	}

	if (serial)
	{
		unsigned int matches = 0;
		if (!VR_FBT_SerialIsSafe (serial))
		{
			Con_Printf ("FBT: serial selector must contain a safe serial\n");
			return;
		}
		for (unsigned int index = 0; index < VR_FBT_GetCandidateCount (&vr_input_fbt_manager); ++index)
		{
			vr_fbt_candidate_status_t status;
			if (VR_FBT_GetCandidate (&vr_input_fbt_manager, index, &status) &&
				status.has_safe_serial && !strcmp (status.serial, serial))
			{
				candidate = (int)index;
				++matches;
			}
		}
		if (matches > 1)
		{
			Con_Printf ("FBT: safe serial is ambiguous among current tracker candidates\n");
			return;
		}
		if (matches == 1)
		{
			if (!VR_FBT_AssignCandidate (&vr_input_fbt_manager, role, (unsigned int)candidate))
			{
				Con_Printf ("FBT: tracker is ambiguous or already assigned to another role\n");
				return;
			}
		}
		else if (!VR_FBT_BindSerial (&vr_input_fbt_manager, role, serial))
		{
			Con_Printf ("FBT: safe serial is already assigned to another role\n");
			return;
		}
	}
	Con_Printf ("FBT: assigned %s\n", VR_InputFBTRoleName (role));
}

static void VR_InputFBTUnassign_f (void)
{
	vr_fbt_role_t role;
	if (Cmd_Argc () != 2 || !VR_InputFBTParseRole (Cmd_Argv (1), &role))
	{
		Con_Printf ("usage: vr_fbt_unassign <hip|left_foot|right_foot>\n");
		return;
	}
	if (!VR_FBT_UnassignRole (&vr_input_fbt_manager, role))
	{
		Con_Printf ("FBT: unable to unassign role\n");
		return;
	}
	Con_Printf ("FBT: unassigned %s\n", VR_InputFBTRoleName (role));
}

static qboolean VR_InputHandAccepted (const vrxr_frame_t *frame, int hand)
{
	const vr_input_hand_state_t *state;
	const vrxr_input_t *input;

	if (!frame || hand < 0 || hand > 1)
		return false;
	state = &vr_input_hands[hand];
	input = &frame->hands[hand];
	return state->identity_valid && !state->wait_neutral && input->active &&
		state->role == VR_InputRoleForPhysicalHand (hand) && state->profile == input->profile;
}

qboolean VR_InputPhysicalHandAccepted (const vrxr_frame_t *frame,
	int physical_hand)
{
	return VR_InputHandAccepted (frame, physical_hand);
}

static void VR_InputClearPendingContactRecord (usercmd_t *pending)
{
	if (pending)
		memset (&pending->vr_contact, 0, sizeof (pending->vr_contact));
	memset (&vr_input_pending_contact_identity, 0,
		sizeof (vr_input_pending_contact_identity));
}

static void VR_InputClearPendingAkimboRecord (usercmd_t *pending)
{
	if (pending)
	{
		pending->vr_akimbo_active = false;
		pending->vr_akimbo_berserk = false;
		memset (pending->vr_akimbo_muzzle, 0,
			sizeof (pending->vr_akimbo_muzzle));
		memset (pending->vr_akimbo_angles, 0,
			sizeof (pending->vr_akimbo_angles));
	}
	memset (&vr_input_pending_akimbo_identity, 0,
		sizeof (vr_input_pending_akimbo_identity));
}

static qboolean VR_InputCalibrationContactAdjustmentActive (void)
{
	const qboolean active = VR_WeaponCalibrationAdjustActive ();

	if (active != vr_input_calibration_contact_adjusting)
	{
		vr_input_calibration_contact_adjusting = active;
		vr_input_contact_discontinuity = true;
		VR_InputClearPendingContactRecord (&cl.pendingcmd);
	}
	return active;
}

static void VR_InputClearPendingRecord (usercmd_t *pending)
{
	if (!pending)
	{
		VR_InputClearPendingContactRecord (NULL);
		VR_InputClearPendingAkimboRecord (NULL);
		return;
	}
	VectorCopy (vec3_origin, pending->vr_handpos);
	VectorCopy (vec3_origin, pending->vr_handrot);
	pending->vr_handpos_relative = false;
	pending->vr_active = false;
	VR_InputClearPendingAkimboRecord (pending);
	pending->vr_pending_move[0] = pending->vr_pending_move[1] = pending->vr_pending_move[2] = 0.0f;
	pending->vr_pending_angles[0] = pending->vr_pending_angles[1] = pending->vr_pending_angles[2] = 0.0f;
	pending->vr_pending_move_valid = false;
	pending->vr_pending_angles_valid = false;
	memset (&pending->vr_gorilla, 0, sizeof (pending->vr_gorilla));
	VR_InputClearPendingContactRecord (pending);
}

static void VR_InputGateMovement (usercmd_t *pending)
{
	vr_input_move_wait_neutral = true;
	if (pending)
	{
		pending->vr_pending_move[0] = pending->vr_pending_move[1] = pending->vr_pending_move[2] = 0.0f;
		pending->vr_pending_move_valid = false;
	}
}

static void VR_InputGateTurn (void)
{
	vr_input_turn_wait_neutral = true;
	vr_input_last_snap = 0;
}

static qboolean VR_InputMotionContextAccepted (const vrxr_frame_t *frame)
{
	if (!frame || !frame->focused || !frame->devices[0].valid ||
		cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback ||
		cl.intermission || cl.paused || key_dest != key_game || Key_InputGrabActive () ||
		!vr_input_context_valid || !VR_InputContextMatchesCurrent (&vr_input_context))
		return false;
	return true;
}

static qboolean VR_InputWireVec (const float value[3])
{
	return value && isfinite (value[0]) && isfinite (value[1]) && isfinite (value[2]) &&
		value[0] >= VR_INPUT_WIRE_MIN && value[0] <= VR_INPUT_WIRE_MAX &&
		value[1] >= VR_INPUT_WIRE_MIN && value[1] <= VR_INPUT_WIRE_MAX &&
		value[2] >= VR_INPUT_WIRE_MIN && value[2] <= VR_INPUT_WIRE_MAX;
}

/* One completed XR sample, in the same world/body basis as the ordinary
 * private hand command. The movement solver owns contact; input only samples. */
static void VR_InputPrepareGorillaSample (usercmd_t *pending,
	const vrxr_frame_t *frame)
{
	vr_gorilla_input_t sample = {0};
	vec3_t head_horizontal, velocity;
	float viewheight, head_height, mapping_yaw, units_per_metre;

	if (!pending || !frame || !vr_gorilla.value ||
		!cl.vr_gorilla_supported || !cl.vr_gorilla_allowed ||
		cl.protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!VR_InputControllerAim () || !VR_InputMotionContextAccepted (frame) ||
		CL_AngleLocked () || cl.stats[STAT_HEALTH] <= 0 ||
		!V_TrackedPlayerBase (&viewheight) ||
		!R_TrackedHeadBodyOffset (head_horizontal) ||
		!R_TrackedHeadEyeHeight (viewheight, &head_height) ||
		!V_TrackedMappingYaw (&mapping_yaw))
		goto unavailable;
	units_per_metre = V_VRUnitsPerMetre ();
	if (!isfinite (units_per_metre) || units_per_metre <= 0.0f)
		goto unavailable;
	VectorCopy (head_horizontal, sample.head);
	sample.head[2] = head_height;
	for (int hand = 0; hand < 2; ++hand)
	{
		const vrxr_device_t *device = &frame->devices[hand + 1];
		if (!VR_InputHandAccepted (frame, hand) || !device->valid ||
			!device->tracked || !device->velocity_valid ||
			!V_TrackedHandBodyOffset (hand, sample.hand[hand]) ||
			!VR_InputFBTMapTrackingVector (device->velocity,
				mapping_yaw, 0.0f, velocity))
			goto unavailable;
		/* V_TrackedHandBodyOffset is head-relative horizontally; restore
		 * the same body-to-head offset carried by the sample's head. */
		VectorAdd (sample.hand[hand], head_horizontal, sample.hand[hand]);
		VectorScale (velocity, units_per_metre, sample.velocity[hand]);
		if (!VR_InputWireVec (sample.hand[hand]) ||
			!VR_InputWireVec (sample.velocity[hand]) ||
			DotProduct (sample.velocity[hand], sample.velocity[hand]) >
				VR_GORILLA_MAX_HAND_SPEED * VR_GORILLA_MAX_HAND_SPEED)
			goto unavailable;
		{
			vec3_t reach;
			VectorSubtract (sample.hand[hand], sample.head, reach);
			if (DotProduct (reach, reach) >
				VR_GORILLA_MAX_REACH * VR_GORILLA_MAX_REACH)
				goto unavailable;
		}
	}
	if (!VR_InputWireVec (sample.head) ||
		DotProduct (sample.head, sample.head) > 160.0f * 160.0f)
		goto unavailable;
	if (frame->reference_changed)
		vr_input_gorilla_discontinuity = true;
	sample.flags = VR_GORILLA_HANDS;
	pending->vr_gorilla = sample;
	return;

unavailable:
	vr_input_gorilla_discontinuity = true;
}

static qboolean VR_InputContactIsValid (const vr_weapon_contact_t *contact)
{
	const unsigned int hand_flags = contact ? contact->flags &
		(VR_WEAPON_CONTACT_LEFT_VALID | VR_WEAPON_CONTACT_RIGHT_VALID) : 0;
	int hand;

	if (!contact ||
		(hand_flags != VR_WEAPON_CONTACT_LEFT_VALID &&
		 hand_flags != VR_WEAPON_CONTACT_RIGHT_VALID) ||
		(contact->flags != hand_flags && contact->flags !=
		(hand_flags | VR_WEAPON_CONTACT_IMMERSIVE_MELEE)) ||
		contact->modelindex < 1 || contact->modelindex > 0xffff ||
		!isfinite (contact->weapon) || contact->weapon < 0.0f ||
		contact->weapon > VR_INPUT_WIRE_MAX)
		return false;

	hand = hand_flags == VR_WEAPON_CONTACT_LEFT_VALID ? 0 : 1;
	return VR_InputWireVec (contact->grip[hand]) &&
		VR_InputWireVec (contact->base[hand]) &&
		VR_InputWireVec (contact->tip[hand]) &&
		isfinite (contact->speed[hand]) && contact->speed[hand] >= 0.0f &&
		contact->speed[hand] <= VR_INPUT_CONTACT_SPEED_MAX;
}

static qboolean VR_InputSelectedStockAxe (int *modelindex_out,
	qmodel_t **model_out, int *skin_out, aliashdr_t **geometry_out,
	stockaxe_edge_t *edge_out)
{
	const int modelindex = cl.stats[STAT_WEAPON];
	qmodel_t *model;
	aliashdr_t *geometry;
	stockaxe_edge_t edge;
	const int skin = cl.viewent.skinnum;

	if (modelindex_out)
		*modelindex_out = 0;
	if (model_out)
		*model_out = NULL;
	if (skin_out)
		*skin_out = -1;
	if (geometry_out)
		*geometry_out = NULL;
	if (edge_out)
		memset (edge_out, 0, sizeof (*edge_out));

	if (!modelindex_out || !model_out || !skin_out || !geometry_out ||
		!edge_out || modelindex < 1 || modelindex >= MAX_MODELS)
		return false;
	model = cl.model_precache[modelindex];
	if (!model || model->needload || model != cl.viewent.model ||
		strcmp (model->name, "progs/v_axe.mdl") || skin < 0)
		return false;
	geometry = (aliashdr_t *)Mod_Extradata_CheckSkin (model, skin);
	if (!geometry || geometry->poseverttype != PV_QUAKE1 ||
		!Mod_GetStockAxeEdge (model, skin, &edge) || !edge.valid)
		return false;

	*modelindex_out = modelindex;
	*model_out = model;
	*skin_out = skin;
	*geometry_out = geometry;
	*edge_out = edge;
	return true;
}

static qboolean VR_InputAliasTransformPoint (const float model_matrix[16],
	const vec3_t point, vec3_t transformed)
{
	if (!model_matrix || !point || !transformed ||
		!isfinite (point[0]) || !isfinite (point[1]) || !isfinite (point[2]))
		return false;
	transformed[0] = model_matrix[0] * point[0] + model_matrix[4] * point[1] +
		model_matrix[8] * point[2] + model_matrix[12];
	transformed[1] = model_matrix[1] * point[0] + model_matrix[5] * point[1] +
		model_matrix[9] * point[2] + model_matrix[13];
	transformed[2] = model_matrix[2] * point[0] + model_matrix[6] * point[1] +
		model_matrix[10] * point[2] + model_matrix[14];
	return isfinite (transformed[0]) && isfinite (transformed[1]) &&
		isfinite (transformed[2]);
}

static qboolean VR_InputRenderOffsetToBody (const vec3_t render_offset,
	float presentation_yaw, float mapping_yaw, vec3_t body_offset)
{
	vec3_t yaw_angles = {0.0f, presentation_yaw, 0.0f};
	vec3_t forward, right, up, tracking_offset;
	if (!render_offset || !body_offset || !isfinite (presentation_yaw) ||
		!isfinite (mapping_yaw) || !VR_InputWireVec (render_offset))
		return false;
	AngleVectors (yaw_angles, forward, right, up);
	/* Undo the visible renderer's yaw basis, then enter the raw command's
	 * mapping-yaw body frame. Both maps are proper rotations (determinant +1). */
	tracking_offset[0] = DotProduct (render_offset, right);
	tracking_offset[1] = DotProduct (render_offset, up);
	tracking_offset[2] = -DotProduct (render_offset, forward);
	return VR_InputWireVec (tracking_offset) &&
		VR_InputFBTMapTrackingVector (tracking_offset, mapping_yaw, 0.0f,
			body_offset) && VR_InputWireVec (body_offset);
}

static qboolean VR_InputContactSpeedBound (const vrxr_device_t *device,
	const vec3_t offset, float units_per_metre, float *speed)
{
	float linear_speed, angular_speed, radius;

	if (!device || !offset || !speed || !isfinite (units_per_metre) ||
		units_per_metre <= 0.0f || !device->velocity_valid ||
		!device->angular_velocity_valid ||
		!isfinite (device->velocity[0]) ||
		!isfinite (device->velocity[1]) ||
		!isfinite (device->velocity[2]) ||
		!isfinite (device->angular_velocity[0]) ||
		!isfinite (device->angular_velocity[1]) ||
		!isfinite (device->angular_velocity[2]) ||
		!VR_InputWireVec (offset))
		return false;
	linear_speed = sqrtf (device->velocity[0] * device->velocity[0] +
		device->velocity[1] * device->velocity[1] +
		device->velocity[2] * device->velocity[2]);
	angular_speed = sqrtf (device->angular_velocity[0] *
		device->angular_velocity[0] + device->angular_velocity[1] *
		device->angular_velocity[1] + device->angular_velocity[2] *
		device->angular_velocity[2]);
	radius = sqrtf (offset[0] * offset[0] + offset[1] * offset[1] +
		offset[2] * offset[2]) / units_per_metre;
	/* Rotation-invariant upper bound for a rigid point's physical speed. This
	 * is collision contact speed, not a melee strike-speed estimate. */
	*speed = linear_speed + angular_speed * radius;
	return isfinite (linear_speed) && isfinite (angular_speed) &&
		isfinite (radius) && isfinite (*speed) && *speed >= 0.0f &&
		*speed <= VR_INPUT_CONTACT_SPEED_MAX;
}

static qboolean VR_InputContactPointSpeed (const vrxr_device_t *device,
	const vec3_t body_offset, float mapping_yaw,
	float units_per_metre, float *speed)
{
	vec3_t linear_body, angular_body, offset_metres, rotational_velocity;
	vec3_t point_velocity;
	if (!device || !body_offset || !speed ||
		!isfinite (mapping_yaw) || !isfinite (units_per_metre) ||
		units_per_metre <= 0.0f || !device->velocity_valid ||
		!device->angular_velocity_valid || !VR_InputWireVec (body_offset) ||
		!isfinite (device->velocity[0]) || !isfinite (device->velocity[1]) ||
		!isfinite (device->velocity[2]) ||
		!isfinite (device->angular_velocity[0]) ||
		!isfinite (device->angular_velocity[1]) ||
		!isfinite (device->angular_velocity[2]) ||
		!VR_InputFBTMapTrackingVector (device->velocity, mapping_yaw,
			0.0f, linear_body) ||
		!VR_InputFBTMapTrackingVector (device->angular_velocity,
			mapping_yaw, 0.0f, angular_body))
		return false;

	for (int axis = 0; axis < 3; ++axis)
		offset_metres[axis] = body_offset[axis] / units_per_metre;
	rotational_velocity[0] = angular_body[1] * offset_metres[2] -
		angular_body[2] * offset_metres[1];
	rotational_velocity[1] = angular_body[2] * offset_metres[0] -
		angular_body[0] * offset_metres[2];
	rotational_velocity[2] = angular_body[0] * offset_metres[1] -
		angular_body[1] * offset_metres[0];
	for (int axis = 0; axis < 3; ++axis)
		point_velocity[axis] = linear_body[axis] + rotational_velocity[axis];
	*speed = sqrtf (point_velocity[0] * point_velocity[0] +
		point_velocity[1] * point_velocity[1] +
		point_velocity[2] * point_velocity[2]);
	return isfinite (offset_metres[0]) && isfinite (offset_metres[1]) &&
		isfinite (offset_metres[2]) && isfinite (rotational_velocity[0]) &&
		isfinite (rotational_velocity[1]) && isfinite (rotational_velocity[2]) &&
		VR_InputWireVec (point_velocity) && isfinite (*speed) && *speed >= 0.0f &&
		*speed <= VR_INPUT_CONTACT_SPEED_MAX;
}

static qboolean VR_InputPrepareCollisionContact (usercmd_t *pending,
	const vrxr_frame_t *frame, int hand, const vec3_t grip,
	const vec3_t world_muzzle, int modelindex, qmodel_t *model)
{
	vr_weapon_contact_t contact;
	const vrxr_device_t *device;
	const vr_input_hand_state_t *hand_state;
	float units_per_metre, contact_speed;
	int weapon;
	int flag;

	if (!pending || !frame || hand < 0 || hand > 1 || !model ||
		!frame->sample_id || cl.protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		VR_WeaponCalibrationAdjustActive () ||
		!VR_WeaponCollisionAuthorized () || !VR_InputControllerAim () ||
		!V_TrackedSessionActive () ||
		!VR_InputMotionContextAccepted (frame) || CL_AngleLocked () ||
		!pending->vr_active || !pending->vr_handpos_relative ||
		!VR_InputWireVec (pending->vr_handpos) ||
		!VR_InputWireVec (grip) || !VR_InputWireVec (world_muzzle) ||
		cls.state != ca_connected || cls.signon != SIGNONS ||
		cls.demoplayback || cl.intermission || cl.paused ||
		key_dest != key_game || cl.stats[STAT_HEALTH] <= 0 ||
		!cl.worldmodel || cl.worldmodel->needload || !cl.entities ||
		cl.viewentity <= 0 || cl.viewentity >= cl.num_entities ||
		modelindex != cl.stats[STAT_WEAPON] || modelindex < 1 ||
		modelindex >= MAX_MODELS || cl.model_precache[modelindex] != model ||
		!VR_WeaponCalibrationStockRangedViewmodel (model->name) ||
		!VR_InputHandAccepted (frame, hand) ||
		!frame->focused || !frame->should_render ||
		!frame->devices[0].valid || !frame->devices[0].tracked ||
		!frame->devices[hand + 1].valid ||
		!frame->devices[hand + 1].tracked ||
		frame->devices[hand + 1].hand != hand)
		return false;

	weapon = cl.stats[STAT_ACTIVEWEAPON];
	if (weapon < 0 || weapon > (int)VR_INPUT_WIRE_MAX)
		return false;
	device = &frame->devices[hand + 1];
	hand_state = &vr_input_hands[hand];
	units_per_metre = V_VRUnitsPerMetre ();
	if (!isfinite (units_per_metre) || units_per_metre <= 0.0f)
		return false;

	memset (&contact, 0, sizeof (contact));
	flag = hand == 0 ? VR_WEAPON_CONTACT_LEFT_VALID :
		VR_WEAPON_CONTACT_RIGHT_VALID;
	contact.flags = (unsigned int)flag;
	contact.modelindex = modelindex;
	contact.weapon = (float)weapon;
	VectorCopy (grip, contact.grip[hand]);
	VectorCopy (grip, contact.base[hand]);
	VectorAdd (grip, world_muzzle, contact.tip[hand]);
	if (!VR_InputContactSpeedBound (device, world_muzzle, units_per_metre,
		&contact_speed))
		return false;
	contact.speed[hand] = contact_speed;
	if (!VR_InputContactIsValid (&contact))
		return false;

	pending->vr_contact = contact;
	vr_input_pending_contact_identity.sample_id = frame->sample_id;
	vr_input_pending_contact_identity.model = model;
	vr_input_pending_contact_identity.modelindex = modelindex;
	vr_input_pending_contact_identity.weapon = weapon;
	vr_input_pending_contact_identity.hand = hand;
	vr_input_pending_contact_identity.role = hand_state->role;
	vr_input_pending_contact_identity.profile = hand_state->profile;
	vr_input_pending_contact_identity.device_hand = device->hand;
	vr_input_pending_contact_identity.reset_generation =
		vr_input_reset_generation;
	strncpy (vr_input_pending_contact_identity.model_name, model->name,
		sizeof (vr_input_pending_contact_identity.model_name) - 1);
	vr_input_pending_contact_identity.model_name[
		sizeof (vr_input_pending_contact_identity.model_name) - 1] = '\0';
	memcpy (vr_input_pending_contact_identity.serial, device->serial,
		sizeof (vr_input_pending_contact_identity.serial));
	return true;
}

static qboolean VR_InputPrepareMeleeContact (usercmd_t *pending,
	const vrxr_frame_t *frame, int hand, const vec3_t grip, int modelindex,
	qmodel_t *model, int skin, aliashdr_t *geometry,
	const stockaxe_edge_t *edge)
{
	vr_weapon_contact_t contact;
	const vrxr_device_t *device;
	const vr_input_hand_state_t *hand_state;
	lerpdata_t lerpdata;
	vec3_t hand_angles, model_angles, raw_base, raw_tip;
	vec3_t render_base, render_tip, body_base, body_tip;
	float model_matrix[16], presentation_yaw, mapping_yaw;
	float units_per_metre, point_speed;
	int weapon;

	if (!pending || !frame || hand < 0 || hand > 1 || !model || !geometry ||
		!edge || !edge->valid || !frame->sample_id ||
		VR_WeaponCalibrationAdjustActive () ||
		!VR_InputMeleeAuthorized () ||
		!VR_InputMotionContextAccepted (frame) || CL_AngleLocked () ||
		!V_TrackedSessionActive () || !VR_InputControllerAim () ||
		!pending->vr_active || !pending->vr_handpos_relative ||
		!VR_InputWireVec (pending->vr_handpos) || !VR_InputWireVec (grip) ||
		!VR_InputHandAccepted (frame, hand) || !frame->focused ||
		!frame->should_render || !frame->devices[0].valid ||
		!frame->devices[0].tracked || frame->devices[0].kind != VRXR_DEVICE_HEAD ||
		frame->devices[0].hand != -1 || !frame->devices[hand + 1].valid ||
		!frame->devices[hand + 1].tracked ||
		frame->devices[hand + 1].kind != VRXR_DEVICE_HAND ||
		frame->devices[hand + 1].hand != hand ||
		modelindex != cl.stats[STAT_WEAPON] || modelindex < 1 ||
		modelindex >= MAX_MODELS || cl.model_precache[modelindex] != model ||
		cl.viewent.model != model || cl.viewent.skinnum != skin ||
		cl.stats[STAT_HEALTH] <= 0 || !cl.worldmodel ||
		cl.worldmodel->needload || !cl.entities || cl.viewentity <= 0 ||
		cl.viewentity >= cl.num_entities || !isfinite (vr_gunmodelpitch.value) ||
		!V_TrackedPresentationYaw (&presentation_yaw) ||
		!V_TrackedMappingYaw (&mapping_yaw) ||
		!V_TrackedPresentationHandAngles (hand, hand_angles) ||
		!VR_LocomotionHandRotToViewmodelAngles (hand_angles, model_angles,
			vr_gunmodelpitch.value))
		return false;

	weapon = cl.stats[STAT_ACTIVEWEAPON];
	if (weapon < 0 || weapon > (int)VR_INPUT_WIRE_MAX)
		return false;
	device = &frame->devices[hand + 1];
	hand_state = &vr_input_hands[hand];
	units_per_metre = V_VRUnitsPerMetre ();
	if (!isfinite (units_per_metre) || units_per_metre <= 0.0f)
		return false;

	/* Reuse the same held-model matrix the renderer submits. The cached points
	 * already include hdr->scale and hdr->scale_origin, so recover the raw MDL
	 * vertices before that matrix applies those header transforms once. */
	memset (&lerpdata, 0, sizeof (lerpdata));
	VectorCopy (model_angles, lerpdata.angles);
	if (R_AliasModelMatrix (&cl.viewent, geometry, &lerpdata, model_matrix) < 0)
		return false;
	for (int axis = 0; axis < 3; ++axis)
	{
		if (!isfinite (geometry->scale[axis]) || geometry->scale[axis] == 0.0f ||
			!isfinite (geometry->scale_origin[axis]))
			return false;
		raw_base[axis] = (edge->base[axis] - geometry->scale_origin[axis]) /
			geometry->scale[axis];
		raw_tip[axis] = (edge->tip[axis] - geometry->scale_origin[axis]) /
			geometry->scale[axis];
	}
	if (!VR_InputAliasTransformPoint (model_matrix, raw_base, render_base) ||
		!VR_InputAliasTransformPoint (model_matrix, raw_tip, render_tip) ||
		!VR_InputRenderOffsetToBody (render_base, presentation_yaw,
			mapping_yaw, body_base) ||
		!VR_InputRenderOffsetToBody (render_tip, presentation_yaw,
			mapping_yaw, body_tip) ||
		!VR_InputContactPointSpeed (device, body_tip, mapping_yaw,
			units_per_metre, &point_speed))
		return false;

	memset (&contact, 0, sizeof (contact));
	contact.flags = (hand == 0 ? VR_WEAPON_CONTACT_LEFT_VALID :
		VR_WEAPON_CONTACT_RIGHT_VALID) | VR_WEAPON_CONTACT_IMMERSIVE_MELEE;
	contact.modelindex = modelindex;
	contact.weapon = (float)weapon;
	VectorCopy (grip, contact.grip[hand]);
	VectorAdd (grip, body_base, contact.base[hand]);
	VectorAdd (grip, body_tip, contact.tip[hand]);
	contact.speed[hand] = point_speed;
	if (!VR_InputContactIsValid (&contact))
		return false;

	pending->vr_contact = contact;
	vr_input_pending_contact_identity.sample_id = frame->sample_id;
	vr_input_pending_contact_identity.model = model;
	vr_input_pending_contact_identity.modelindex = modelindex;
	vr_input_pending_contact_identity.weapon = weapon;
	vr_input_pending_contact_identity.hand = hand;
	vr_input_pending_contact_identity.role = hand_state->role;
	vr_input_pending_contact_identity.profile = hand_state->profile;
	vr_input_pending_contact_identity.device_hand = device->hand;
	vr_input_pending_contact_identity.skin = skin;
	vr_input_pending_contact_identity.geometry = geometry;
	vr_input_pending_contact_identity.reset_generation =
		vr_input_reset_generation;
	strncpy (vr_input_pending_contact_identity.model_name, model->name,
		sizeof (vr_input_pending_contact_identity.model_name) - 1);
	vr_input_pending_contact_identity.model_name[
		sizeof (vr_input_pending_contact_identity.model_name) - 1] = '\0';
	memcpy (vr_input_pending_contact_identity.serial, device->serial,
		sizeof (vr_input_pending_contact_identity.serial));
	return true;
}

static qboolean VR_InputPendingContactAccepted (const usercmd_t *pending,
	const vrxr_frame_t *frame)
{
	const int hand = vr_input_pending_contact_identity.hand;
	const int modelindex = cl.stats[STAT_WEAPON];
	const unsigned int hand_flag = hand == 0 ?
		VR_WEAPON_CONTACT_LEFT_VALID : VR_WEAPON_CONTACT_RIGHT_VALID;
	const unsigned int contact_flags = pending ? pending->vr_contact.flags : 0;
	const qboolean immersive =
		(contact_flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) != 0;
	const vrxr_device_t *device;
	qmodel_t *model;
	qmodel_t *selected_axe;
	aliashdr_t *selected_geometry;
	stockaxe_edge_t selected_edge;
	int selected_axe_index, selected_skin;

	if (!pending || !frame || !frame->sample_id ||
		VR_WeaponCalibrationAdjustActive () ||
		vr_input_pending_contact_identity.sample_id != frame->sample_id ||
		vr_input_pending_contact_identity.reset_generation !=
			vr_input_reset_generation ||
		vr_input_pending_contact_identity.modelindex != modelindex ||
		modelindex < 1 || modelindex >= MAX_MODELS ||
		!VR_InputMotionContextAccepted (frame) || CL_AngleLocked () ||
		!V_TrackedSessionActive () || !pending->vr_active ||
		!pending->vr_handpos_relative || hand < 0 || hand > 1 ||
		!VR_InputContactIsValid (&pending->vr_contact) ||
		pending->vr_contact.modelindex != modelindex ||
		pending->vr_contact.weapon !=
			(float)cl.stats[STAT_ACTIVEWEAPON] ||
		vr_input_pending_contact_identity.weapon !=
			cl.stats[STAT_ACTIVEWEAPON] ||
		vr_input_pending_contact_identity.role !=
			vr_input_hands[hand].role ||
		vr_input_pending_contact_identity.profile !=
			vr_input_hands[hand].profile ||
		!VR_InputHandAccepted (frame, hand) ||
		!frame->focused || !frame->should_render ||
		!frame->devices[0].valid || !frame->devices[0].tracked ||
		!frame->devices[hand + 1].valid ||
		!frame->devices[hand + 1].tracked ||
		frame->devices[hand + 1].kind != VRXR_DEVICE_HAND ||
		frame->devices[hand + 1].hand != hand ||
		(contact_flags != hand_flag && contact_flags !=
		(hand_flag | VR_WEAPON_CONTACT_IMMERSIVE_MELEE)))
		return false;

	model = cl.model_precache[modelindex];
	device = &frame->devices[hand + 1];
	if (!model || model != vr_input_pending_contact_identity.model ||
		strcmp (model->name, vr_input_pending_contact_identity.model_name) ||
		device->hand != vr_input_pending_contact_identity.device_hand ||
		device->hand != hand ||
		memcmp (device->serial, vr_input_pending_contact_identity.serial,
			sizeof (device->serial)) != 0)
		return false;

	if (immersive)
		return VR_InputMeleeAuthorized () && VR_InputControllerAim () &&
			cl.stats[STAT_HEALTH] > 0 &&
			hand == VR_InputDominantPhysicalHand () &&
			frame->devices[0].kind == VRXR_DEVICE_HEAD &&
			frame->devices[0].hand == -1 &&
			VR_InputSelectedStockAxe (&selected_axe_index, &selected_axe,
				&selected_skin, &selected_geometry, &selected_edge) &&
			selected_axe_index == modelindex && selected_axe == model &&
			selected_skin == vr_input_pending_contact_identity.skin &&
			selected_geometry == vr_input_pending_contact_identity.geometry;

	return VR_WeaponCollisionAuthorized () &&
		VR_WeaponCalibrationStockRangedViewmodel (model->name);
}

static qboolean VR_InputVRIKMatrixFinite (const float matrix[3][4])
{
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 4; ++column)
			if (!isfinite (matrix[row][column]))
				return false;
	return true;
}

static void VR_InputVRIKRotMatFromAngles (const vec3_t angles, float matrix[3][3])
{
	vec3_t mutable_angles;

	VectorCopy (angles, mutable_angles);
	AngleVectors (mutable_angles, matrix[0], matrix[1], matrix[2]);
	/* Match the donor matrix convention: zero angles produce identity. */
	for (int axis = 0; axis < 3; ++axis)
		matrix[1][axis] = -matrix[1][axis];
}

static qboolean VR_InputVRIKAnglesFromRotMat (float matrix[3][3], vec3_t angles)
{
	vec3_t unrolled_angles;
	float unrolled_matrix[3][3];

	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 3; ++column)
			if (!isfinite (matrix[row][column]))
				return false;
	angles[YAW] = (float)(-atan2 (matrix[0][0], matrix[0][1]) / M_PI_DIV_180 + 90.0);
	angles[PITCH] = (float)(atan2 (sqrt (matrix[0][0] * matrix[0][0] +
		matrix[0][1] * matrix[0][1]), matrix[0][2]) / M_PI_DIV_180 - 90.0);
	angles[ROLL] = 0.0f;
	VectorCopy (angles, unrolled_angles);
	VR_InputVRIKRotMatFromAngles (unrolled_angles, unrolled_matrix);
	angles[ROLL] = (float)(-atan2 (DotProduct (unrolled_matrix[1], matrix[1]),
		DotProduct (unrolled_matrix[2], matrix[1])) / M_PI_DIV_180 + 90.0);
	return isfinite (angles[PITCH]) && isfinite (angles[YAW]) && isfinite (angles[ROLL]);
}

static qboolean VR_InputVRIKRootLocalAngles (const vec3_t world_angles,
	float body_yaw, vec3_t out)
{
	float world_matrix[3][3], inverse_body_yaw[3][3], local_matrix[3][3];
	vec3_t inverse_angles = {0.0f, -body_yaw, 0.0f}, angles;

	VR_InputVRIKRotMatFromAngles (world_angles, world_matrix);
	VR_InputVRIKRotMatFromAngles (inverse_angles, inverse_body_yaw);
	R_ConcatRotations (world_matrix, inverse_body_yaw, local_matrix);
	if (!VR_InputVRIKAnglesFromRotMat (local_matrix, angles))
		return false;
	for (int axis = 0; axis < 3; ++axis)
	{
		while (angles[axis] > 180.0f)
			angles[axis] -= 360.0f;
		while (angles[axis] < -180.0f)
			angles[axis] += 360.0f;
	}
	VectorCopy (angles, out);
	return true;
}

static qboolean VR_InputFBTMatrixQuaternion (const float matrix[3][4],
	double quaternion[4])
{
	double trace, scale;

	if (!matrix || !quaternion || !VR_InputFBTMatrixFinite (matrix))
		return false;
	trace = (double)matrix[0][0] + matrix[1][1] + matrix[2][2];
	if (trace > 0.0)
	{
		scale = sqrt (trace + 1.0) * 2.0;
		if (!isfinite (scale) || scale <= 0.0001) return false;
		quaternion[0] = 0.25 * scale;
		quaternion[1] = (matrix[2][1] - matrix[1][2]) / scale;
		quaternion[2] = (matrix[0][2] - matrix[2][0]) / scale;
		quaternion[3] = (matrix[1][0] - matrix[0][1]) / scale;
	}
	else if (matrix[0][0] > matrix[1][1] && matrix[0][0] > matrix[2][2])
	{
		scale = sqrt (1.0 + matrix[0][0] - matrix[1][1] - matrix[2][2]) * 2.0;
		if (!isfinite (scale) || scale <= 0.0001) return false;
		quaternion[0] = (matrix[2][1] - matrix[1][2]) / scale;
		quaternion[1] = 0.25 * scale;
		quaternion[2] = (matrix[0][1] + matrix[1][0]) / scale;
		quaternion[3] = (matrix[0][2] + matrix[2][0]) / scale;
	}
	else if (matrix[1][1] > matrix[2][2])
	{
		scale = sqrt (1.0 + matrix[1][1] - matrix[0][0] - matrix[2][2]) * 2.0;
		if (!isfinite (scale) || scale <= 0.0001) return false;
		quaternion[0] = (matrix[0][2] - matrix[2][0]) / scale;
		quaternion[1] = (matrix[0][1] + matrix[1][0]) / scale;
		quaternion[2] = 0.25 * scale;
		quaternion[3] = (matrix[1][2] + matrix[2][1]) / scale;
	}
	else
	{
		scale = sqrt (1.0 + matrix[2][2] - matrix[0][0] - matrix[1][1]) * 2.0;
		if (!isfinite (scale) || scale <= 0.0001) return false;
		quaternion[0] = (matrix[1][0] - matrix[0][1]) / scale;
		quaternion[1] = (matrix[0][2] + matrix[2][0]) / scale;
		quaternion[2] = (matrix[1][2] + matrix[2][1]) / scale;
		quaternion[3] = 0.25 * scale;
	}
	scale = sqrt (quaternion[0] * quaternion[0] + quaternion[1] * quaternion[1] +
		quaternion[2] * quaternion[2] + quaternion[3] * quaternion[3]);
	if (!isfinite (scale) || scale <= 0.0001)
		return false;
	for (int component = 0; component < 4; ++component)
	{
		quaternion[component] /= scale;
		if (!isfinite (quaternion[component]))
			return false;
	}
	return true;
}

static void VR_InputFBTQuaternionMatrix (const double quaternion[4],
	float matrix[3][4])
{
	const double w = quaternion[0], x = quaternion[1];
	const double y = quaternion[2], z = quaternion[3];
	matrix[0][0] = (float)(1.0 - 2.0 * (y * y + z * z));
	matrix[0][1] = (float)(2.0 * (x * y - w * z));
	matrix[0][2] = (float)(2.0 * (x * z + w * y));
	matrix[1][0] = (float)(2.0 * (x * y + w * z));
	matrix[1][1] = (float)(1.0 - 2.0 * (x * x + z * z));
	matrix[1][2] = (float)(2.0 * (y * z - w * x));
	matrix[2][0] = (float)(2.0 * (x * z - w * y));
	matrix[2][1] = (float)(2.0 * (y * z + w * x));
	matrix[2][2] = (float)(1.0 - 2.0 * (x * x + y * y));
	for (int row = 0; row < 3; ++row)
		matrix[row][3] = 0.0f;
}

static uint64_t VR_InputFBTSerialIdentity (const char *serial)
{
	uint64_t hash = UINT64_C (1469598103934665603);
	const unsigned char *cursor = (const unsigned char *)serial;
	if (!VR_FBT_SerialIsSafe (serial))
		return 0;
	while (*cursor)
	{
		hash ^= *cursor++;
		hash *= UINT64_C (1099511628211);
	}
	return hash ? hash : 1;
}

static qboolean VR_InputFBTRawTransform (const vr_fbt_role_status_t *status,
	vr_fbt_profile_transform_t *transform)
{
	double quaternion[4];
	if (!status || !transform || !status->connected || !status->pose_valid ||
		status->state != VR_FBT_STATE_TRACKING ||
		status->tracking_result != VR_FBT_TRACKING_RESULT_RUNNING_OK ||
		status->identity_kind != VR_FBT_IDENTITY_SERIAL ||
		!VR_FBT_SerialIsSafe (status->serial) ||
		!VR_InputFBTMatrixQuaternion (status->device_to_absolute_tracking,
			quaternion))
		return false;
	transform->position[0] = status->device_to_absolute_tracking[0][3];
	transform->position[1] = status->device_to_absolute_tracking[1][3];
	transform->position[2] = status->device_to_absolute_tracking[2][3];
	for (int component = 0; component < 4; ++component)
		transform->orientation[component] = quaternion[component];
	return isfinite (transform->position[0]) && isfinite (transform->position[1]) &&
		isfinite (transform->position[2]);
}

static qboolean VR_InputFBTProjectionInput (const vrxr_frame_t *frame,
	entity_t **player_out, r_vrik_calibration_projection_input_t *input)
{
	const vrxr_device_t *head;
	entity_t *player;
	vec3_t cross;
	float forward_length;
	if (!frame || !player_out || !input || !frame->should_render ||
		!frame->focused || !frame->floor_referenced || !cl.entities ||
		cl.viewentity <= 0 || cl.viewentity >= cl.num_entities ||
	cls.state != ca_connected || cls.signon != SIGNONS)
		return false;
	head = &frame->devices[0];
	player = &cl.entities[cl.viewentity];
	if (!head->valid || !head->tracked || head->kind != VRXR_DEVICE_HEAD ||
		head->hand != -1 || !VR_InputFBTMatrixFinite (head->matrix) ||
		!player->model)
		return false;
	memset (input, 0, sizeof (*input));
	for (int axis = 0; axis < 3; ++axis)
		input->hmd_position[axis] = head->matrix[axis][3];
	input->forward[0] = -head->matrix[0][2];
	input->forward[1] = 0.0f;
	input->forward[2] = -head->matrix[2][2];
	forward_length = VectorNormalize (input->forward);
	if (!isfinite (forward_length) || forward_length < 0.01f)
		return false;
	input->up[0] = 0.0f; input->up[1] = 1.0f; input->up[2] = 0.0f;
	CrossProduct (input->forward, input->up, cross);
	if (!VectorNormalize (cross))
		return false;
	VectorCopy (cross, input->right);
	input->floor_height = 0.0f;
	*player_out = player;
	return true;
}

static qboolean VR_InputFBTProjectReference (const vrxr_frame_t *frame,
	entity_t **player, r_vrik_calibration_projection_input_t *input,
	r_vrik_calibration_projection_t *projection)
{
	return projection && VR_InputFBTProjectionInput (frame, player, input) &&
		R_VRIKProjectCalibrationReference ((*player)->model, input, projection);
}

static qboolean VR_InputFBTCalibrationBindingsReady (
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX], unsigned int *role_mask)
{
	unsigned int mask = 0;
	if (!serials || !role_mask)
		return false;
	memset (serials, 0, sizeof (char) * VR_FBT_ROLE_COUNT * VR_FBT_SERIAL_MAX);
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		vr_fbt_role_status_t status;
		if (!VR_FBT_GetRoleStatus (&vr_input_fbt_manager,
			(vr_fbt_role_t)role, &status))
			return false;
		if (status.identity_kind != VR_FBT_IDENTITY_SERIAL ||
			status.state != VR_FBT_STATE_TRACKING || !status.connected ||
			!status.pose_valid ||
			status.tracking_result != VR_FBT_TRACKING_RESULT_RUNNING_OK)
			continue;
		if (!VR_FBT_SerialIsSafe (status.serial))
			return false;
		memcpy (serials[role], status.serial, sizeof (serials[role]));
		mask |= VR_FBT_PROFILE_ROLE_BIT (role);
	}
	*role_mask = mask;
	return mask != 0;
}

static qboolean VR_InputFBTCalibrateBegin (const char *name,
	qboolean menu_generated_name)
{
	char serials[VR_FBT_ROLE_COUNT][VR_FBT_SERIAL_MAX];
	const char *expected[VR_FBT_ROLE_COUNT];
	unsigned int role_mask;
	vr_fbt_profile_capture_t capture;
	r_vrik_calibration_projection_input_t projection_input;
	r_vrik_calibration_projection_t projection;
	entity_t *player;
	const vrxr_frame_t *frame = GL_OpenXRFrame ();

	if (!VR_FBT_StorageNameIsSafe (name))
	{
		Con_Printf ("FBT: calibration profile name is invalid\n");
		return false;
	}
	if (!vr_fbt_enabled.value)
	{
		Con_Printf ("FBT: enable full body tracking before calibration\n");
		return false;
	}
	if (!VR_InputFBTCalibrationBindingsReady (serials, &role_mask))
	{
		Con_Printf ("FBT: assign at least one tracking safe-serial role before calibration\n");
		return false;
	}
	if (!VR_InputFBTProjectReference (frame, &player, &projection_input,
		&projection))
	{
		Con_Printf ("FBT: verified Ranger floor-reference projection is unavailable; capture not started\n");
		return false;
	}
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		expected[role] = serials[role];
	if (!VR_FBT_ProfileCaptureBegin (&capture, role_mask, expected))
	{
		Con_Printf ("FBT: calibration setup failed; current profile kept\n");
		return false;
	}
	vr_input_fbt_capture = capture;
	memset (&vr_input_fbt_visual_raw_snapshot, 0,
		sizeof (vr_input_fbt_visual_raw_snapshot));
	q_strlcpy (vr_input_fbt_calibration_name, name,
		sizeof (vr_input_fbt_calibration_name));
	vr_input_fbt_menu_calibration_name = menu_generated_name;
	vr_input_fbt_preview_valid = false;
	vr_input_fbt_calibration_state = VR_INPUT_FBT_CALIBRATION_READY;
	vr_input_fbt_capture_last_sample_id = frame->sample_id;
	Con_Printf ("FBT: calibration ready for %s; stand neutral, then use vr_fbt_calibrate_capture\n",
		vr_input_fbt_calibration_name);
	return true;
}

static void VR_InputFBTCalibrateBegin_f (void)
{
	if (Cmd_Argc () != 2 || !VR_FBT_StorageNameIsSafe (Cmd_Argv (1)))
	{
		Con_Printf ("usage: vr_fbt_calibrate_begin <[A-Za-z0-9_-]{1,32}>\n");
		return;
	}
	VR_InputFBTCalibrateBegin (Cmd_Argv (1), false);
}

qboolean VR_InputFBTBeginMenuCalibration (void)
{
	char name[VR_FBT_PROFILE_NAME_MAX];
	vr_fbt_profile_t existing;
	vr_fbt_profile_error_t profile_error;
	vr_fbt_storage_error_t storage_error;
	if (!vr_input_fbt_initialized)
		return false;
	if (vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_IDLE)
	{
		Con_Printf ("FBT: accept or cancel the current calibration first\n");
		return false;
	}
	for (unsigned int suffix = 1; suffix <= 999; ++suffix)
	{
		q_snprintf (name, sizeof (name), "menu_fbt_%u", suffix);
		if (VR_FBT_StorageLoadProfile (name, &existing, &profile_error,
			&storage_error))
			continue;
		if (storage_error == VR_FBT_STORAGE_ERR_NOT_FOUND)
			return VR_InputFBTCalibrateBegin (name, true);
		/* A malformed file still occupies its name; skip it instead of
		 * allowing accept to replace user data. Other storage failures are
		 * ambiguous, so stop without beginning a calibration. */
		if (storage_error != VR_FBT_STORAGE_ERR_FORMAT)
		{
			Con_Printf ("FBT: could not choose a new calibration profile name (%d)\n",
				(int)storage_error);
			return false;
		}
	}
	Con_Printf ("FBT: no unused menu calibration profile name is available\n");
	return false;
}

static void VR_InputFBTCalibrateCapture_f (void)
{
	if (Cmd_Argc () != 1)
	{
		Con_Printf ("usage: vr_fbt_calibrate_capture\n");
		return;
	}
	if (vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_READY)
	{
		Con_Printf ("FBT: begin calibration first\n");
		return;
	}
	vr_input_fbt_calibration_state = VR_INPUT_FBT_CALIBRATION_CAPTURING;
	if (GL_OpenXRFrame ())
		vr_input_fbt_capture_last_sample_id = GL_OpenXRFrame ()->sample_id;
	Con_Printf ("FBT: capturing one second of neutral tracker poses\n");
}

static void VR_InputFBTCaptureSnapshot (const vrxr_frame_t *frame)
{
	vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT];
	r_vrik_calibration_projection_input_t projection_input;
	r_vrik_calibration_projection_t projection;
	entity_t *player;
	vr_fbt_profile_capture_metadata_t metadata;
	vr_fbt_profile_error_t profile_error;
	if (vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_CAPTURING ||
		!frame || !frame->sample_id || frame->sample_id == vr_input_fbt_capture_last_sample_id ||
		frame->sample_id != vr_input_fbt_last_seen_sample_id)
		return;
	if (!VR_InputFBTProjectReference (frame, &player, &projection_input,
		&projection))
	{
		VR_InputFBTCancelCalibration ();
		Con_Printf ("FBT: verified Ranger floor-reference was lost; calibration cancelled\n");
		return;
	}
	vr_input_fbt_capture_last_sample_id = frame->sample_id;
	memset (samples, 0, sizeof (samples));
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		vr_fbt_role_status_t status;
		vr_fbt_profile_capture_sample_t *sample = &samples[role];
		if (!(vr_input_fbt_capture.required_role_mask & VR_FBT_PROFILE_ROLE_BIT (role)))
			continue;
		sample->present = 1;
		if (!VR_FBT_GetRoleStatus (&vr_input_fbt_manager,
			(vr_fbt_role_t)role, &status))
			continue;
		sample->connected = status.connected;
		sample->pose_valid = VR_InputFBTRawTransform (&status,
			&sample->raw_tracker_transform);
		if (status.identity_kind == VR_FBT_IDENTITY_SERIAL &&
			VR_FBT_SerialIsSafe (status.serial))
			memcpy (sample->serial, status.serial, sizeof (sample->serial));
		for (int axis = 0; axis < 3; ++axis)
		{
			sample->reference_target_transform.position[axis] =
				projection.position[role][axis];
			sample->linear_velocity_metres_per_second[axis] =
				isfinite (status.velocity[axis]) ? status.velocity[axis] : 0.0;
			sample->angular_velocity_radians_per_second[axis] =
				isfinite (status.angular_velocity[axis]) ? status.angular_velocity[axis] : 0.0;
		}
		for (int component = 0; component < 4; ++component)
			sample->reference_target_transform.orientation[component] =
				projection.orientation_wxyz[role][component];
	}
	if (!VR_FBT_ProfileCaptureAddSnapshot (&vr_input_fbt_capture,
		vr_input_fbt_snapshot_id, vr_input_fbt_snapshot_time, samples))
		return;
	if (!vr_input_fbt_capture.started ||
		vr_input_fbt_snapshot_time - vr_input_fbt_capture.first_snapshot_time < 1.0)
		return;
	memset (&metadata, 0, sizeof (metadata));
	q_strlcpy (metadata.name, vr_input_fbt_calibration_name, sizeof (metadata.name));
	VR_InputFBTCopySafeSerial (metadata.hmd_serial, frame->devices[0].serial);
	metadata.hmd_height_metres = projection_input.hmd_position[1] -
		projection_input.floor_height;
	metadata.floor_height_metres = projection_input.floor_height;
	for (int axis = 0; axis < 3; ++axis)
		metadata.body_forward[axis] = projection_input.forward[axis];
	if (!VR_FBT_ProfileCaptureFinalize (&vr_input_fbt_capture, &metadata,
		&vr_input_fbt_preview_profile, &profile_error))
	{
		VR_InputFBTCancelCalibration ();
		Con_Printf ("FBT: neutral capture was unstable or incomplete; current profile kept (%d)\n",
			(int)profile_error);
		return;
	}
	vr_input_fbt_preview_valid = true;
	vr_input_fbt_calibration_state = VR_INPUT_FBT_CALIBRATION_PREVIEW;
	Con_Printf ("FBT: calibration preview ready; use vr_fbt_calibrate_accept to save\n");
}

static void VR_InputFBTCalibrateAccept_f (void)
{
	vr_fbt_profile_t preview;
	vr_fbt_profile_t existing;
	vr_fbt_storage_error_t storage_error = VR_FBT_STORAGE_OK;
	vr_fbt_profile_error_t profile_error = VR_FBT_PROFILE_OK;
	qboolean not_durable, profile_visible;
	if (Cmd_Argc () != 1)
	{
		Con_Printf ("usage: vr_fbt_calibrate_accept\n");
		return;
	}
	if (vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_PREVIEW ||
		!vr_input_fbt_preview_valid)
	{
		Con_Printf ("FBT: no completed calibration preview to accept\n");
		return;
	}
	if (vr_input_fbt_menu_calibration_name)
	{
		if (VR_FBT_StorageLoadProfile (vr_input_fbt_calibration_name, &existing,
			&profile_error, &storage_error) ||
			storage_error != VR_FBT_STORAGE_ERR_NOT_FOUND)
		{
			Con_Printf ("FBT: generated calibration name is no longer unused; current profile kept\n");
			return;
		}
	}
	preview = vr_input_fbt_preview_profile;
	if (!VR_InputFBTSaveAndSelectProfile (&preview, &profile_error,
		&storage_error, &not_durable, &profile_visible))
	{
		Con_Printf ("FBT: accepted calibration was not selected (%d); current profile kept%s\n",
			(int)storage_error, profile_visible ? "; saved file remains visible" : "");
		return;
	}
	if (not_durable)
		Con_Warning ("FBT: accepted calibration is visible but not crash-durable\n");
	Con_Printf ("FBT: accepted and saved calibration %s\n",
		vr_input_fbt_profile.name);
}

static void VR_InputFBTCalibrateCancel_f (void)
{
	if (Cmd_Argc () != 1)
	{
		Con_Printf ("usage: vr_fbt_calibrate_cancel\n");
		return;
	}
	if (vr_input_fbt_calibration_state == VR_INPUT_FBT_CALIBRATION_IDLE)
	{
		Con_Printf ("FBT: no calibration is in progress\n");
		return;
	}
	VR_InputFBTCancelCalibration ();
	Con_Printf ("FBT: calibration cancelled; current profile kept\n");
}

static qboolean VR_InputFBTMapPointToRoot (const vrxr_frame_t *frame,
	const vec3_t point, float presentation_yaw, float body_yaw,
	float units_per_metre, float head_eye_height, vec3_t root_position)
{
	const vrxr_device_t *head;
	vec3_t head_position, head_body_offset, mapped;
	float radians, cosine, sine;
	if (!frame || !point || !root_position || !isfinite (presentation_yaw) ||
		!isfinite (body_yaw) || !isfinite (units_per_metre) ||
		units_per_metre <= 0.0f || !isfinite (head_eye_height))
		return false;
	head = &frame->devices[0];
	if (!head->valid || !head->tracked || !VR_InputFBTMatrixFinite (head->matrix))
		return false;
	for (int axis = 0; axis < 3; ++axis)
	{
		head_position[axis] = head->matrix[axis][3];
		if (!isfinite (point[axis]))
			return false;
	}
	if (!VR_LocomotionHandBodyOffset (head_position, point, presentation_yaw,
		units_per_metre, head_eye_height, mapped) ||
		!R_TrackedHeadBodyOffset (head_body_offset))
		return false;
	mapped[0] += head_body_offset[0];
	mapped[1] += head_body_offset[1];
	radians = body_yaw * M_PI_DIV_180;
	cosine = cosf (radians);
	sine = sinf (radians);
	root_position[0] = mapped[0] * cosine + mapped[1] * sine;
	root_position[1] = -mapped[0] * sine + mapped[1] * cosine;
	root_position[2] = mapped[2];
	return VR_InputWireVec (root_position);
}

/* Map a direction or velocity through the same OpenXR-to-Quake basis used by
 * VR_LocomotionHandBodyOffset, then from world yaw into the sender root. */
static qboolean VR_InputFBTMapTrackingVector (const vec3_t tracking,
	float presentation_yaw, float body_yaw, vec3_t root)
{
	vec3_t angles = {0.0f, presentation_yaw, 0.0f};
	vec3_t forward, right, up, world;
	float radians, cosine, sine;
	if (!tracking || !root || !VR_InputFBTVectorFinite (tracking) ||
		!isfinite (presentation_yaw) || !isfinite (body_yaw))
		return false;
	AngleVectors (angles, forward, right, up);
	world[0] = right[0] * tracking[0] + up[0] * tracking[1] - forward[0] * tracking[2];
	world[1] = right[1] * tracking[0] + up[1] * tracking[1] - forward[1] * tracking[2];
	world[2] = right[2] * tracking[0] + up[2] * tracking[1] - forward[2] * tracking[2];
	radians = body_yaw * M_PI_DIV_180;
	cosine = cosf (radians);
	sine = sinf (radians);
	root[0] = world[0] * cosine + world[1] * sine;
	root[1] = -world[0] * sine + world[1] * cosine;
	root[2] = world[2];
	return VR_InputFBTVectorFinite (root);
}

/* Called only by VR_InputCommands on the input/main owner, after reconciling
 * this completed XR sample. The render setup task consumes these detached raw
 * positions; it never reads the mutable tracker manager. */
static void VR_InputFBTPrepareCalibrationVisualSnapshot (const vrxr_frame_t *frame)
{
	const unsigned int valid_role_mask = (1u << VR_FBT_ROLE_COUNT) - 1u;
	const char (*expected_serials)[VR_FBT_SERIAL_MAX] = NULL;
	unsigned int role_mask = 0;
	memset (&vr_input_fbt_visual_raw_snapshot, 0,
		sizeof (vr_input_fbt_visual_raw_snapshot));
	if (!vr_fbt_enabled.value || !frame || !frame->sample_id ||
		!frame->should_render || !frame->focused || !frame->floor_referenced ||
		frame->reference_changed ||
		(vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_READY &&
		 vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_CAPTURING &&
		 vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_PREVIEW))
		return;
	if (vr_input_fbt_calibration_state == VR_INPUT_FBT_CALIBRATION_PREVIEW)
	{
		if (!vr_input_fbt_preview_valid)
			return;
		for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
			if (vr_input_fbt_preview_profile.roles[role].present)
				role_mask |= VR_FBT_PROFILE_ROLE_BIT (role);
	}
	else
	{
		role_mask = vr_input_fbt_capture.required_role_mask;
		expected_serials = vr_input_fbt_capture.expected_serials;
	}
	if (!role_mask || (role_mask & ~valid_role_mask))
		return;
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		vr_fbt_role_status_t status;
		vr_fbt_profile_transform_t raw;
		const char *expected;
		if (!(role_mask & VR_FBT_PROFILE_ROLE_BIT (role)))
			continue;
		expected = vr_input_fbt_calibration_state == VR_INPUT_FBT_CALIBRATION_PREVIEW ?
			vr_input_fbt_preview_profile.roles[role].serial : expected_serials[role];
		if (!VR_FBT_SerialIsSafe (expected) ||
			!VR_FBT_GetRoleStatus (&vr_input_fbt_manager,
				(vr_fbt_role_t)role, &status) ||
			status.identity_kind != VR_FBT_IDENTITY_SERIAL ||
			strcmp (status.serial, expected) ||
			!VR_InputFBTRawTransform (&status, &raw))
		{
			memset (&vr_input_fbt_visual_raw_snapshot, 0,
				sizeof (vr_input_fbt_visual_raw_snapshot));
			return;
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			vr_input_fbt_visual_raw_snapshot.tracker_tracking[role][axis] =
				(float)raw.position[axis];
			if (!isfinite (vr_input_fbt_visual_raw_snapshot.tracker_tracking[role][axis]))
			{
				memset (&vr_input_fbt_visual_raw_snapshot, 0,
					sizeof (vr_input_fbt_visual_raw_snapshot));
				return;
			}
		}
	}
	vr_input_fbt_visual_raw_snapshot.role_mask = role_mask;
	vr_input_fbt_visual_raw_snapshot.sample_id = frame->sample_id;
}

qboolean VR_InputFBTCalibrationVisualSnapshot (const vrxr_frame_t *frame,
	vr_input_fbt_visual_snapshot_t *snapshot)
{
	vr_input_fbt_visual_snapshot_t prepared;
	r_vrik_calibration_projection_input_t projection_input;
	r_vrik_calibration_projection_t projection;
	const unsigned int valid_role_mask = (1u << VR_FBT_ROLE_COUNT) - 1u;
	float body_yaw, presentation_yaw, base_viewheight, head_eye_height;
	float units_per_metre;
	uint64_t expected_sample_id;
	entity_t *player;
	vec3_t point, root;
	if (!snapshot)
		return false;
	memset (snapshot, 0, sizeof (*snapshot));
	if (!vr_fbt_enabled.value || !frame || !frame->should_render ||
		!frame->focused || !frame->floor_referenced || frame->reference_changed ||
		(vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_READY &&
		 vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_CAPTURING &&
		 vr_input_fbt_calibration_state != VR_INPUT_FBT_CALIBRATION_PREVIEW) ||
		!vr_input_fbt_visual_raw_snapshot.role_mask ||
		!vr_input_fbt_visual_raw_snapshot.sample_id ||
		(vr_input_fbt_visual_raw_snapshot.role_mask & ~valid_role_mask))
		return false;
	expected_sample_id = vr_input_fbt_visual_raw_snapshot.sample_id + 1;
	if (!expected_sample_id)
		++expected_sample_id;
	if (frame->sample_id != expected_sample_id)
		return false;
	memset (&prepared, 0, sizeof (prepared));
	if (!VR_InputFBTProjectReference (frame, &player, &projection_input,
		&projection) ||
		!V_TrackedPlayerBase (&base_viewheight) ||
		!V_TrackedPresentationYaw (&presentation_yaw) ||
		!R_TrackedHeadEyeHeight (base_viewheight, &head_eye_height))
		return false;
	body_yaw = player->angles[YAW];
	if (!isfinite (body_yaw))
		body_yaw = cl.viewangles[YAW];
	units_per_metre = V_VRUnitsPerMetre ();
	for (int axis = 0; axis < 3; ++axis)
		point[axis] = frame->devices[0].matrix[axis][3];
	if (!isfinite (body_yaw) || !isfinite (presentation_yaw) ||
		!isfinite (units_per_metre) || units_per_metre <= 0.0f ||
		!VR_InputFBTMapPointToRoot (frame, point, presentation_yaw, body_yaw,
			units_per_metre, head_eye_height, root))
		return false;
	for (int axis = 0; axis < 3; ++axis)
	{
		prepared.head_root_metres[axis] = root[axis] / units_per_metre;
		if (!isfinite (prepared.head_root_metres[axis]) ||
			fabsf (prepared.head_root_metres[axis]) > VR_INPUT_WIRE_MAX)
			return false;
	}
	prepared.body_yaw_degrees = body_yaw;
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		if (!(vr_input_fbt_visual_raw_snapshot.role_mask &
			VR_FBT_PROFILE_ROLE_BIT (role)))
			continue;
		VectorCopy (vr_input_fbt_visual_raw_snapshot.tracker_tracking[role], point);
		if (!VR_InputFBTMapPointToRoot (frame, point, presentation_yaw,
			body_yaw, units_per_metre, head_eye_height, root))
			return false;
		for (int axis = 0; axis < 3; ++axis)
		{
			prepared.tracker_root_metres[role][axis] = root[axis] / units_per_metre;
			point[axis] = projection.position[role][axis];
		}
		if (!VR_InputFBTMapPointToRoot (frame, point, presentation_yaw,
			body_yaw, units_per_metre, head_eye_height, root))
			return false;
		for (int axis = 0; axis < 3; ++axis)
		{
			prepared.target_root_metres[role][axis] = root[axis] / units_per_metre;
			if (!isfinite (prepared.tracker_root_metres[role][axis]) ||
				!isfinite (prepared.target_root_metres[role][axis]) ||
				fabsf (prepared.tracker_root_metres[role][axis]) > VR_INPUT_WIRE_MAX ||
				fabsf (prepared.target_root_metres[role][axis]) > VR_INPUT_WIRE_MAX)
				return false;
		}
	}
	prepared.role_mask = vr_input_fbt_visual_raw_snapshot.role_mask;
	*snapshot = prepared;
	return true;
}

static qboolean VR_InputFBTBuildFilterInput (const vrxr_frame_t *frame,
	int role, float body_yaw, float presentation_yaw, float units_per_metre,
	float head_eye_height, vr_fbt_filter_input_t *input)
{
	vr_fbt_role_status_t status;
	vr_fbt_profile_transform_t raw, corrected;
	const vr_fbt_profile_role_entry_t *binding;
	vec3_t world_angles, root_angles, mapped, point;
	float rotation[3][3];
	uint64_t identity;
	if (!input || role < 0 || role >= VR_FBT_ROLE_COUNT ||
		!VR_FBT_GetRoleStatus (&vr_input_fbt_manager, (vr_fbt_role_t)role, &status))
		return false;
	binding = &vr_input_fbt_profile.roles[role];
	if (!binding->present || status.identity_kind != VR_FBT_IDENTITY_SERIAL ||
		!VR_FBT_SerialIsSafe (status.serial) || strcmp (status.serial, binding->serial))
	{
		VR_FBT_FilterResetRole (&vr_input_fbt_filter,
			(vr_fbt_filter_role_t)role);
		memset (&vr_input_fbt_cached_targets[role], 0,
			sizeof (vr_input_fbt_cached_targets[role]));
		return false;
	}
	identity = VR_InputFBTSerialIdentity (status.serial);
	input->snapshot_id = vr_input_fbt_snapshot_id;
	input->snapshot_time = vr_input_fbt_snapshot_time;
	input->identity = identity;
	input->identity_valid = identity != 0;
	input->connected = status.connected;
	input->floor_valid = frame->floor_referenced;
	input->floor_height = V_VRFloorOffset () / units_per_metre;
	input->root_yaw_degrees = body_yaw;
	input->root_yaw_valid = isfinite (body_yaw);
	if (!VR_InputFBTRawTransform (&status, &raw) ||
		!VR_FBT_ProfileApplyCorrection (&raw, &binding->device_to_anatomical,
			&corrected))
		return true;
	for (int axis = 0; axis < 3; ++axis)
	{
		point[axis] = (float)corrected.position[axis];
		if (!isfinite (point[axis]))
			return true;
	}
	if (!VR_InputFBTMapPointToRoot (frame, point,
		presentation_yaw, body_yaw, units_per_metre, head_eye_height, mapped))
		return true;
	for (int axis = 0; axis < 3; ++axis)
		input->position[axis] = mapped[axis] / units_per_metre;
	{
		float orientation_matrix[3][4];
		VR_InputFBTQuaternionMatrix (corrected.orientation, orientation_matrix);
		if (!VR_AimPoseAngles (orientation_matrix, presentation_yaw, world_angles) ||
			!VR_InputVRIKRootLocalAngles (world_angles, body_yaw, root_angles))
			return true;
		VR_InputVRIKRotMatFromAngles (root_angles, rotation);
		{
			float root_matrix[3][4] = {
				{rotation[0][0], rotation[0][1], rotation[0][2], 0.0f},
				{rotation[1][0], rotation[1][1], rotation[1][2], 0.0f},
				{rotation[2][0], rotation[2][1], rotation[2][2], 0.0f}
			};
			double quaternion[4];
			if (!VR_InputFBTMatrixQuaternion (root_matrix, quaternion))
				return true;
			for (int component = 0; component < 4; ++component)
				input->orientation[component] = (float)quaternion[component];
		}
	}
	input->tracking_valid = 1;
	/* The profile correction is a rigid transform. Its translation offset is
	 * expressed in tracking metres, so include omega x offset before mapping. */
	{
		float corrected_linear[3], point_offset[3];
		vec3_t velocity_root;
		for (int axis = 0; axis < 3; ++axis)
			point_offset[axis] = (float)(corrected.position[axis] - raw.position[axis]);
		if (VR_FBT_FilterCorrectedPointVelocity (corrected_linear,
			status.velocity, status.angular_velocity, point_offset) &&
			VR_InputFBTMapTrackingVector (corrected_linear,
				presentation_yaw, body_yaw, velocity_root))
			VectorCopy (velocity_root, input->linear_velocity);
	}
	{
		vec3_t angular_root;
		if (VR_InputFBTMapTrackingVector (status.angular_velocity,
			presentation_yaw, body_yaw, angular_root))
			VectorCopy (angular_root, input->angular_velocity);
	}
	return true;
}

static void VR_InputFBTAppendTargets (const vrxr_frame_t *frame,
	entity_t *player, vrik_codec_pose_t *pose, float body_yaw,
	float presentation_yaw, float base_viewheight, float units_per_metre)
{
	float head_eye_height;
	double now;
	if (!frame || !player || !pose || !vr_fbt_enabled.value ||
		!vr_input_fbt_profile_valid || !frame->floor_referenced ||
		!player->model ||
		!R_VRIKCalibrationReferenceAvailable (player->model) ||
		!R_TrackedHeadEyeHeight (base_viewheight, &head_eye_height))
	{
		if (vr_input_fbt_sender_ready)
			VR_InputFBTResetFilterState ();
		if (vr_input_fbt_calibration_state == VR_INPUT_FBT_CALIBRATION_CAPTURING)
			VR_InputFBTCancelCalibration ();
		return;
	}
	if (!isfinite (units_per_metre) || units_per_metre <= 0.0f ||
		!isfinite (body_yaw) || !isfinite (presentation_yaw))
	{
		if (vr_input_fbt_sender_ready)
			VR_InputFBTResetFilterState ();
		return;
	}
	vr_input_fbt_sender_ready = true;
	if (frame->sample_id && frame->sample_id == vr_input_fbt_last_seen_sample_id &&
		frame->sample_id != vr_input_fbt_filter_last_sample_id)
	{
		/* Claim this completed OpenXR sample before visiting roles. Repeated
		 * pose builds reuse output without advancing any role filter. */
		vr_input_fbt_filter_last_sample_id = frame->sample_id;
		for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
		{
			vr_fbt_filter_input_t input;
			vr_fbt_filter_output_t output;
			memset (&input, 0, sizeof (input));
			if (!VR_InputFBTBuildFilterInput (frame, role, body_yaw,
				presentation_yaw, units_per_metre, head_eye_height, &input))
				continue;
			if (VR_FBT_FilterUpdate (&vr_input_fbt_filter,
				(vr_fbt_filter_role_t)role, &input, &output))
			{
				vr_input_fbt_cached_targets[role].output = output;
				vr_input_fbt_cached_targets[role].sample_id = frame->sample_id;
				vr_input_fbt_cached_targets[role].output_time = input.snapshot_time;
				vr_input_fbt_cached_targets[role].present =
					output.state != VR_FBT_FILTER_STATE_LOST;
			}
		}
	}
	now = Sys_DoubleTime ();
	if (!isfinite (now))
		now = vr_input_fbt_snapshot_time;
	for (int role = 0; role < VR_FBT_ROLE_COUNT; ++role)
	{
		vr_input_fbt_cached_target_t *cached = &vr_input_fbt_cached_targets[role];
		const int target = VRIK_TARGET_HIP + role;
		vec3_t angles;
		double age;
		float length_squared;
		if (!cached->present || !isfinite (cached->output_time) ||
			now < cached->output_time)
			continue;
		age = now - cached->output_time;
		if (age > VR_FBT_FILTER_HOLD_SECONDS ||
			cached->output.state == VR_FBT_FILTER_STATE_LOST)
		{
			VR_FBT_FilterResetRole (&vr_input_fbt_filter,
				(vr_fbt_filter_role_t)role);
			memset (cached, 0, sizeof (*cached));
			continue;
		}
		for (int axis = 0; axis < 3; ++axis)
			pose->targets[target].position[axis] =
				cached->output.position[axis] * units_per_metre;
		length_squared = DotProduct (pose->targets[target].position,
			pose->targets[target].position);
		if (!VR_InputWireVec (pose->targets[target].position) ||
			!isfinite (length_squared) ||
			length_squared > VRIK_MAX_ROOT_LOCAL_OFFSET * VRIK_MAX_ROOT_LOCAL_OFFSET)
		{
			memset (&pose->targets[target], 0, sizeof (pose->targets[target]));
			VR_FBT_FilterResetRole (&vr_input_fbt_filter,
				(vr_fbt_filter_role_t)role);
			memset (cached, 0, sizeof (*cached));
			continue;
		}
		{
			float quaternion_matrix[3][4];
			float rotation_matrix[3][3];
			double norm = 0.0;
			qboolean quaternion_finite = true;
			for (int component = 0; component < 4; ++component)
			{
				if (!isfinite (cached->output.orientation[component]))
				{
					quaternion_finite = false;
					break;
				}
				norm += (double)cached->output.orientation[component] *
					cached->output.orientation[component];
			}
			if (!quaternion_finite || !isfinite (norm) || norm < 0.5 || norm > 1.5)
			{
				memset (&pose->targets[target], 0, sizeof (pose->targets[target]));
				VR_FBT_FilterResetRole (&vr_input_fbt_filter,
					(vr_fbt_filter_role_t)role);
				memset (cached, 0, sizeof (*cached));
				continue;
			}
			{
				double quaternion[4];
				for (int component = 0; component < 4; ++component)
					quaternion[component] = cached->output.orientation[component];
				VR_InputFBTQuaternionMatrix (quaternion, quaternion_matrix);
			}
			for (int row = 0; row < 3; ++row)
				for (int column = 0; column < 3; ++column)
					rotation_matrix[row][column] = quaternion_matrix[row][column];
			if (!VR_InputVRIKAnglesFromRotMat (rotation_matrix, angles))
			{
				memset (&pose->targets[target], 0, sizeof (pose->targets[target]));
				VR_FBT_FilterResetRole (&vr_input_fbt_filter,
					(vr_fbt_filter_role_t)role);
				memset (cached, 0, sizeof (*cached));
				continue;
			}
		}
		VectorCopy (angles, pose->targets[target].orientation);
		pose->present_mask |= VRIK_TARGET_BIT (target);
		if (cached->output.tracked && cached->sample_id == frame->sample_id &&
			age <= VR_FBT_FILTER_PREDICT_SECONDS)
			pose->tracked_mask |= VRIK_TARGET_BIT (target);
	}
}

/* Build from the retained completed OpenXR frame. The view owner supplies
 * presentation yaw and canonical head/hand body offsets; player yaw defines
 * the VRIK root-local frame. */
qboolean VR_InputBuildVRIKPose (vrik_codec_pose_t *pose)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const vrxr_device_t *head;
	entity_t *player;
	float body_yaw, presentation_yaw, base_viewheight, head_eye_height;
	float yaw_radians, cosine, sine;
	vec3_t head_world_angles, mapped_hand_angles, head_world_offset, local_position;
	int dominant;

	if (!pose)
		return false;
	memset (pose, 0, sizeof (*pose));
	if (!frame || !frame->should_render || !frame->focused ||
		cls.state != ca_connected || cls.signon != SIGNONS || cls.demoplayback ||
		cl.intermission || !cl.worldmodel || !cl.entities || cl.viewentity <= 0 ||
		cl.viewentity >= cl.num_entities || cl.stats[STAT_HEALTH] <= 0 ||
		!V_TrackedPlayerBase (&base_viewheight) ||
		!V_TrackedPresentationYaw (&presentation_yaw))
		return false;

	head = &frame->devices[0];
	player = &cl.entities[cl.viewentity];
	if (!player->model || !head->valid || !head->tracked ||
		head->kind != VRXR_DEVICE_HEAD || head->hand != -1 ||
		!VR_InputVRIKMatrixFinite (head->matrix))
		return false;

	if (!isfinite (base_viewheight) ||
		!R_TrackedHeadBodyOffset (head_world_offset) ||
		!R_TrackedHeadEyeHeight (base_viewheight, &head_eye_height))
		return false;

	/* A disabled sender emits one inactive sample after its last active pose. */
	if (vr_vrik.value == 0.0f)
		return true;

	body_yaw = player->angles[YAW];
	if (!isfinite (body_yaw))
		body_yaw = cl.viewangles[YAW];
	if (!isfinite (body_yaw))
		return false;

	pose->sequence = cl.vrik_next_sequence;
	pose->flags = VRIK_V3_FLAG_ACTIVE;
	pose->present_mask = VRIK_TARGET_BIT (VRIK_TARGET_HEAD);
	pose->tracked_mask = VRIK_TARGET_BIT (VRIK_TARGET_HEAD);
	pose->body_yaw = body_yaw;
	if (!VR_AimPoseAngles (head->matrix, presentation_yaw, head_world_angles))
		return false;
	if (!VR_InputVRIKRootLocalAngles (head_world_angles, body_yaw,
		pose->targets[VRIK_TARGET_HEAD].orientation))
		return false;

	yaw_radians = body_yaw * M_PI_DIV_180;
	cosine = cosf (yaw_radians);
	sine = sinf (yaw_radians);
	local_position[0] = head_world_offset[0] * cosine + head_world_offset[1] * sine;
	local_position[1] = -head_world_offset[0] * sine + head_world_offset[1] * cosine;
	local_position[2] = head_eye_height;
	VectorCopy (local_position, pose->targets[VRIK_TARGET_HEAD].position);

	dominant = VR_InputDominantPhysicalHand ();
	if (dominant == VR_INPUT_ROLE_LEFT)
		pose->flags |= VRIK_V3_FLAG_DOMINANT_LEFT;
	for (int hand = 0; hand < 2; ++hand)
	{
		const vrxr_device_t *device = &frame->devices[hand + 1];
		const int target = hand == VR_INPUT_ROLE_LEFT ?
			VRIK_TARGET_LEFT_HAND : VRIK_TARGET_RIGHT_HAND;
		if (!device->valid || !device->tracked || device->kind != VRXR_DEVICE_HAND ||
			device->hand != hand || !VR_InputVRIKMatrixFinite (device->matrix))
			continue;
		if (!V_TrackedPresentationHandBodyOffset (hand, local_position))
			return false;
		{
			const float world_x = local_position[0];
			const float world_y = local_position[1];
			local_position[0] = world_x * cosine + world_y * sine;
			local_position[1] = -world_x * sine + world_y * cosine;
		}
		VectorCopy (local_position, pose->targets[target].position);
		if (!VR_AimPoseAngles (device->matrix, presentation_yaw, mapped_hand_angles))
			return false;
		if (!VR_InputVRIKRootLocalAngles (mapped_hand_angles, body_yaw,
			pose->targets[target].orientation))
			return false;
		pose->present_mask |= VRIK_TARGET_BIT (target);
		pose->tracked_mask |= VRIK_TARGET_BIT (target);
	}

	if (frame->devices[dominant + 1].valid && frame->devices[dominant + 1].tracked &&
		V_TrackedPresentationHandAngles (dominant, mapped_hand_angles))
		if (!VR_InputVRIKRootLocalAngles (mapped_hand_angles, body_yaw,
			pose->aim_orientation))
			return false;

	if (!isfinite (head_eye_height))
		return false;
	VR_InputFBTAppendTargets (frame, player, pose, body_yaw, presentation_yaw,
		base_viewheight, V_VRUnitsPerMetre ());
	return true;
}

static void VR_InputAccumulateRoomscaleMove (const vrxr_frame_t *frame, usercmd_t *pending)
{
	const float *position;
	vec3_t delta, tracking_move, accumulated;
	float tracking_yaw, units_per_metre, yaw_radians, cosine, sine;
	float horizontal_length;

	if (!pending)
		return;

	if (!VR_InputMotionContextAccepted (frame) || CL_AngleLocked ())
	{
		vr_input_roomscale_position_valid = false;
		VectorCopy (vec3_origin, pending->vr_roomscalemove);
		return;
	}

	position = frame->devices[0].matrix[0] + 3;
	if (!isfinite (position[0]) || !isfinite (frame->devices[0].matrix[1][3]) ||
		!isfinite (frame->devices[0].matrix[2][3]))
	{
		vr_input_roomscale_position_valid = false;
		return;
	}

	if (frame->reference_changed || !vr_input_roomscale_position_valid)
	{
		vr_input_roomscale_last_position[0] = position[0];
		vr_input_roomscale_last_position[1] = frame->devices[0].matrix[1][3];
		vr_input_roomscale_last_position[2] = frame->devices[0].matrix[2][3];
		vr_input_roomscale_position_valid = true;
		return;
	}

	delta[0] = position[0] - vr_input_roomscale_last_position[0];
	delta[1] = frame->devices[0].matrix[1][3] - vr_input_roomscale_last_position[1];
	delta[2] = frame->devices[0].matrix[2][3] - vr_input_roomscale_last_position[2];
	vr_input_roomscale_last_position[0] = position[0];
	vr_input_roomscale_last_position[1] = frame->devices[0].matrix[1][3];
	vr_input_roomscale_last_position[2] = frame->devices[0].matrix[2][3];

	if (!VR_InputControllerAim ())
		return;

	units_per_metre = V_VRUnitsPerMetre ();
	if (!isfinite (units_per_metre) || units_per_metre <= 0.0f ||
		!V_TrackedMappingYaw (&tracking_yaw) || !isfinite (tracking_yaw))
		return;

	/* OpenXR position is right/up/backward; inherit the donor's horizontal
	 * mapping, then rotate it by the view owner's mapped tracking yaw. */
	tracking_move[0] = -delta[2] * units_per_metre;
	tracking_move[1] = -delta[0] * units_per_metre;
	tracking_move[2] = 0.0f;
	yaw_radians = tracking_yaw * 0.01745329251994329577f;
	cosine = cosf (yaw_radians);
	sine = sinf (yaw_radians);
	accumulated[0] = tracking_move[0] * cosine - tracking_move[1] * sine;
	accumulated[1] = tracking_move[0] * sine + tracking_move[1] * cosine;
	accumulated[2] = 0.0f;
	horizontal_length = sqrtf (accumulated[0] * accumulated[0] + accumulated[1] * accumulated[1]);
	if (!VR_InputWireVec (accumulated) || !isfinite (horizontal_length) ||
		horizontal_length > VR_INPUT_ROOM_SCALE_MAX_DELTA_UNITS)
	{
		Con_DPrintf ("VR input: ignored room-scale tracking sample outside PMove range\n");
		return;
	}

	accumulated[0] += pending->vr_roomscalemove[0];
	accumulated[1] += pending->vr_roomscalemove[1];
	accumulated[2] += pending->vr_roomscalemove[2];
	/* Preserve the signed sum across no-send frames. Rejecting just the step
	 * that crosses the PMove limit would invent motion if the player returns. */
	if (!VR_InputWireVec (accumulated))
	{
		Con_DPrintf ("VR input: ignored room-scale sample exceeding command encoding range\n");
		return;
	}
	VectorCopy (accumulated, pending->vr_roomscalemove);
}

static qboolean VR_InputRoomscaleCommandAccepted (const vec3_t move)
{
	float horizontal_length;
	if (!VR_InputWireVec (move))
		return false;
	horizontal_length = sqrtf (move[0] * move[0] + move[1] * move[1]);
	return isfinite (horizontal_length) && horizontal_length <= VR_INPUT_ROOM_SCALE_MAX_DELTA_UNITS &&
		fabsf (move[2]) <= VR_INPUT_ROOM_SCALE_MAX_DELTA_UNITS;
}

static void VR_InputTurn180_f (void)
{
	// Ignore commands issued while no gameplay accumulation will run; never
	// retain a console/menu request to execute after a later connection.
	vr_input_turn180_queued = VR_InputMotionContextAccepted (GL_OpenXRFrame ()) &&
		!CL_AngleLocked () && VR_InputFiniteCvar (&vr_180_snap_turn, 1.f) != 0.f;
}

static qboolean VR_InputKeyOwnedByOtherHand (int hand, int key)
{
	return vr_input_hands[1 - hand].owned[key];
}

static qboolean VR_InputMenuHapticKey (int key)
{
	/* The native menu accepts A/B as its select/back aliases for Enter/Escape. */
	return key == K_ENTER || key == K_MOUSE1 || key == K_ESCAPE || key == K_LEFTARROW ||
		key == K_RIGHTARROW || key == K_UPARROW || key == K_DOWNARROW ||
		key == K_ABUTTON || key == K_BBUTTON;
}

static qboolean VR_InputReleaseHand (int hand, unsigned int dispatch_epoch)
{
	vr_input_hand_state_t *state = &vr_input_hands[hand];

	for (int key = 0; key < MAX_KEYS; ++key)
	{
		unsigned int generation;

		if (!state->owned[key])
			continue;
		state->owned[key] = false;
		if (VR_InputKeyOwnedByOtherHand (hand, key) || !vr_input_emitted[key])
			continue;

		vr_input_emitted[key] = false;
		generation = vr_input_reset_generation;
		Key_Event (key, false);
		if (generation != vr_input_reset_generation ||
			dispatch_epoch != vr_input_dispatch_epoch)
			return false;
	}
	return true;
}

static qboolean VR_InputReleaseAll (unsigned int dispatch_epoch)
{
	return VR_InputReleaseHand (0, dispatch_epoch) &&
		VR_InputReleaseHand (1, dispatch_epoch);
}

static void VR_InputGateHand (int hand)
{
	vr_input_hands[hand].wait_neutral = true;
	vr_input_hands[hand].trigger_down = false;
	vr_input_hands[hand].menu_trigger_key = 0;
	if (VR_InputRoleForPhysicalHand (hand) == VR_INPUT_ROLE_LEFT)
	{
		VR_InputGateMovement (&cl.pendingcmd);
		cl.pendingcmd.vr_pending_angles_valid = false;
	}
	else
	{
		VR_InputGateTurn ();
		if (VR_InputControllerAim () && VR_InputMovementMode () == VR_MOVEMENT_MODE_RAW_INPUT)
		{
			VR_InputGateMovement (&cl.pendingcmd);
			cl.pendingcmd.vr_pending_angles_valid = false;
		}
	}
}

static qboolean VR_InputGateAndReleaseHand (int hand, unsigned int dispatch_epoch)
{
	VR_InputGateHand (hand);
	return VR_InputReleaseHand (hand, dispatch_epoch);
}

static qboolean VR_InputGateAndReleaseAll (unsigned int dispatch_epoch)
{
	VR_InputGateHand (0);
	VR_InputGateHand (1);
	return VR_InputReleaseAll (dispatch_epoch);
}

/* A native callback can change the destination, binding-capture mode, or
 * modal grab while a batch is being dispatched. Release every key already
 * emitted in that batch and leave both hands neutral-gated. */
static qboolean VR_InputAbortForContextChange (unsigned int dispatch_epoch)
{
	vr_input_context = VR_InputCurrentContext ();
	vr_input_context_valid = true;
	if (!VR_InputGateAndReleaseAll (dispatch_epoch))
		return false;
	/* Key-up callbacks can also alter native input state. Record the final
	 * context; the neutral gate remains in force for the next sample. */
	vr_input_context = VR_InputCurrentContext ();
	return false;
}

static qboolean VR_InputNeutral (const vrxr_input_t *input)
{
	const uint32_t buttons = VRXR_BUTTON_TRIGGER | VRXR_BUTTON_GRIP | VRXR_BUTTON_STICK |
		VRXR_BUTTON_PAD | VRXR_BUTTON_PRIMARY | VRXR_BUTTON_SECONDARY | VRXR_BUTTON_MENU |
		VRXR_BUTTON_EXTRA1 | VRXR_BUTTON_EXTRA2;

	if (!input->active || (input->pressed & buttons))
		return false;
	if (!isfinite (input->trigger) || VR_InputTriggerValue (input) >= 0.45f)
		return false;
	if (!VR_InputAxesFinite (input))
		return false;
	return VR_InputFilteredAxis (input, 0, 0.0f) == 0.0f &&
		   VR_InputFilteredAxis (input, 1, 0.0f) == 0.0f;
}

static void VR_InputAddKey (qboolean desired[2][MAX_KEYS], int hand, int key)
{
	if (key >= 0 && key < MAX_KEYS)
		desired[hand][key] = true;
}

static void VR_InputAddAxis (qboolean desired[2][MAX_KEYS], int hand, const vrxr_input_t *input,
	int axis, int negative_key, int positive_key, float extra)
{
	const float value = VR_InputFilteredAxis (input, axis, extra);
	if (value < 0.0f)
		VR_InputAddKey (desired, hand, negative_key);
	else if (value > 0.0f)
		VR_InputAddKey (desired, hand, positive_key);
}

static void VR_InputUpdateTrigger (vr_input_hand_state_t *state, const vrxr_input_t *input)
{
	const float value = VR_InputTriggerValue (input);
	if (!state->trigger_down && value > 0.55f)
		state->trigger_down = true;
	else if (state->trigger_down && value < 0.45f)
	{
		state->trigger_down = false;
		state->menu_trigger_key = 0;
	}
}

static void VR_InputBuildHandDesired (qboolean desired[2][MAX_KEYS], int hand,
	const vrxr_input_t *input, const vr_input_context_t *context,
	qboolean suppress_trigger)
{
	vr_input_hand_state_t *state = &vr_input_hands[hand];
	const qboolean logical_left = state->role == VR_INPUT_ROLE_LEFT;
	const uint32_t pressed = input->pressed;
	const uint32_t selected_click = state->profile == VRXR_PROFILE_VIVE ? VRXR_BUTTON_PAD : VRXR_BUTTON_STICK;
	const float axis_extra = VR_InputFiniteCvar (&vr_joystick_axis_menu_deadzone_extra, 0.25f);

	if (context->input_grab)
	{
		if (pressed & (VRXR_BUTTON_SECONDARY | VRXR_BUTTON_MENU))
			VR_InputAddKey (desired, hand, logical_left ? K_ESCAPE : K_BBUTTON);
		if (logical_left && (pressed & VRXR_BUTTON_PRIMARY))
			VR_InputAddKey (desired, hand, K_ABUTTON);

		VR_InputUpdateTrigger (state, input);
		if (!logical_left && state->trigger_down && !suppress_trigger)
			VR_InputAddKey (desired, hand, K_ABUTTON);
		return;
	}

	if (pressed & (VRXR_BUTTON_SECONDARY | VRXR_BUTTON_MENU))
		VR_InputAddKey (desired, hand, logical_left ? K_ESCAPE : K_BBUTTON);
	if (pressed & VRXR_BUTTON_PRIMARY)
		VR_InputAddKey (desired, hand, logical_left ? K_ABUTTON : K_XBUTTON);
	/* The migration bridge deliberately preserves the donor's legacy naming:
	 * Vive PAD and every other profile's STICK are SteamVR_Touchpad. VR_Move
	 * maps that composed click to LTHUMB on the logical left, and on the logical
	 * right to Index ALTFIRE or otherwise RTHUMB. Index PAD is legacy Axis2 and
	 * both logical hands map it to YBUTTON. */
	if (pressed & selected_click)
		VR_InputAddKey (desired, hand,
			logical_left ? K_LTHUMB : (state->profile == VRXR_PROFILE_INDEX ? K_VR_ALTFIRE : K_RTHUMB));
	if (state->profile == VRXR_PROFILE_INDEX && (pressed & VRXR_BUTTON_PAD))
		VR_InputAddKey (desired, hand, K_YBUTTON);
	if (pressed & VRXR_BUTTON_GRIP)
	{
		if (logical_left)
			VR_InputAddKey (desired, hand, K_LSHOULDER);
		else
			VR_InputAddKey (desired, hand, state->profile == VRXR_PROFILE_INDEX ? K_RSHOULDER : K_VR_ALTFIRE);
	}

	VR_InputUpdateTrigger (state, input);
	if (state->trigger_down && !suppress_trigger)
	{
		int trigger_key = logical_left ? K_LTRIGGER : K_RTRIGGER;
		if (!logical_left && context->destination == key_menu)
			trigger_key = context->binding_capture ? K_RTRIGGER : state->menu_trigger_key;
		if (trigger_key)
			VR_InputAddKey (desired, hand, trigger_key);
	}

	if (context->destination == key_menu && logical_left)
	{
		VR_InputAddAxis (desired, hand, input, 0, K_LEFTARROW, K_RIGHTARROW, axis_extra);
		VR_InputAddAxis (desired, hand, input, 1, K_DOWNARROW, K_UPARROW, axis_extra);
	}
	if (!logical_left && (context->destination == key_game || context->destination == key_menu))
		VR_InputAddAxis (desired, hand, input, 1, K_VR_RIGHT_STICK_DOWN, K_VR_RIGHT_STICK_UP, axis_extra);

	/* VRXR exposes EXTRA1/EXTRA2 for future backends, but this key table has no
	 * JOY1..JOY4 keycodes. Do not fabricate a native source or substitute keys. */
}

static qboolean VR_InputEmitDesired (qboolean desired[2][MAX_KEYS],
	const vr_input_context_t *expected_context, unsigned int dispatch_epoch)
{
	for (int phase = 0; phase < 2; ++phase)
	{
		for (int key = 0; key < MAX_KEYS; ++key)
		{
			const qboolean desired_aggregate = desired[0][key] || desired[1][key];
			const qboolean current_aggregate = vr_input_emitted[key];
			const unsigned int generation = vr_input_reset_generation;

			if ((phase == 0 && (desired_aggregate || !current_aggregate)) ||
				(phase == 1 && (!desired_aggregate || current_aggregate)))
				continue;

			vr_input_hands[0].owned[key] = desired[0][key];
			vr_input_hands[1].owned[key] = desired[1][key];
			vr_input_emitted[key] = desired_aggregate;
			if (desired_aggregate && !current_aggregate &&
				((expected_context->destination == key_menu &&
					VR_InputMenuHapticKey (key)) ||
				 (expected_context->destination == key_game && key == K_ESCAPE)) &&
				!expected_context->binding_capture && !expected_context->input_grab)
			{
				const int hand = desired[0][key] ? 0 : 1;
				VR_InputTriggerHaptic (vr_input_hands[hand].role, 0.1f, 0.5f);
				if (generation != vr_input_reset_generation ||
					dispatch_epoch != vr_input_dispatch_epoch)
					return false;
			}
			Key_Event (key, desired_aggregate);

			if (generation != vr_input_reset_generation ||
				dispatch_epoch != vr_input_dispatch_epoch)
				return false;
			if (!VR_InputContextMatchesCurrent (expected_context))
				return VR_InputAbortForContextChange (dispatch_epoch);
		}
	}

	/* Aggregate-equal contributions still need ownership updates so one hand
	 * releasing a duplicated logical key cannot release the other hand's hold. */
	for (int key = 0; key < MAX_KEYS; ++key)
	{
		vr_input_hands[0].owned[key] = desired[0][key];
		vr_input_hands[1].owned[key] = desired[1][key];
	}
	return true;
}

void VR_InputInit (void)
{
	VR_InputFBTReset ();
	Cvar_RegisterVariable (&vr_lefthanded);
	Cvar_RegisterVariable (&vr_haptic);
	Cvar_RegisterVariable (&vr_joystick_axis_deadzone);
	Cvar_RegisterVariable (&vr_joystick_axis_menu_deadzone_extra);
	Cvar_RegisterVariable (&vr_joystick_axis_exponent);
	Cvar_RegisterVariable (&vr_joystick_deadzone_trunc);
	Cvar_RegisterVariable (&vr_movement_mode);
	Cvar_RegisterVariable (&vr_gorilla);
	Cvar_RegisterVariable (&vr_movement_speed);
	Cvar_RegisterVariable (&vr_snap_turn);
	Cvar_RegisterVariable (&vr_180_snap_turn);
	Cvar_RegisterVariable (&vr_turn_speed);
	Cvar_RegisterVariable (&vr_joystick_yaw_multi);
	Cvar_RegisterVariable (&vr_vrik);
	Cvar_RegisterVariable (&vr_immersive_melee);
	Cvar_RegisterVariable (&vr_fbt_enabled);
	Cvar_RegisterVariable (&vr_weapon_collision);
	Cvar_SetCallback (&vr_lefthanded, VR_InputMotionSettingsChanged);
	Cvar_SetCallback (&vr_movement_mode, VR_InputMotionSettingsChanged);
	Cvar_SetCallback (&vr_snap_turn, VR_InputMotionSettingsChanged);
	Cvar_SetCallback (&vr_fbt_enabled, VR_InputFBTEnabledChanged);
	Cmd_AddCommand ("vr_turn180", VR_InputTurn180_f);
	Cmd_AddCommand ("vr_fbt_list", VR_InputFBTList_f);
	Cmd_AddCommand ("vr_fbt_assign", VR_InputFBTAssign_f);
	Cmd_AddCommand ("vr_fbt_unassign", VR_InputFBTUnassign_f);
	Cmd_AddCommand ("vr_fbt_profile_select", VR_InputFBTProfileSelect_f);
	Cmd_AddCommand ("vr_fbt_profile_reset", VR_InputFBTProfileReset_f);
	Cmd_AddCommand ("vr_fbt_profile_list", VR_InputFBTProfileList_f);
	Cmd_AddCommand ("vr_fbt_profile_save", VR_InputFBTProfileSave_f);
	Cmd_AddCommand ("vr_fbt_calibrate_begin", VR_InputFBTCalibrateBegin_f);
	Cmd_AddCommand ("vr_fbt_calibrate_capture", VR_InputFBTCalibrateCapture_f);
	Cmd_AddCommand ("vr_fbt_calibrate_accept", VR_InputFBTCalibrateAccept_f);
	Cmd_AddCommand ("vr_fbt_calibrate_cancel", VR_InputFBTCalibrateCancel_f);
	VR_InputClear ();
	VR_InputFBTLoadSelectedProfile ();
}

void VR_InputCommands (const vrxr_frame_t *frame)
{
	const unsigned int dispatch_epoch = ++vr_input_dispatch_epoch;
	const int dominant = VR_InputDominantPhysicalHand ();
	qboolean desired[2][MAX_KEYS] = {{false}};
	vrxr_input_t input_hands[2];
	vr_input_context_t context;
	/* Reapply the archived preference each input pass: OpenXR teardown resets
	 * runtime state, while this also lets users enable tracking mid-session. */
	VRXR_SetTrackerEnabled (vr_fbt_enabled.value != 0.0f);
	if (vr_fbt_enabled.value && frame && frame->reference_changed && frame->sample_id &&
		frame->sample_id != vr_input_fbt_last_seen_sample_id)
		VR_InputFBTReset ();
	VR_InputFBTReconcile (frame);
	VR_InputFBTCaptureSnapshot (frame);
	VR_InputFBTPrepareCalibrationVisualSnapshot (frame);
	if (cls.state != ca_connected)
	{
		cl.vrik_next_sequence = 0;
		cl.vrik_next_send_time = 0.0;
		cl.vrik_last_sent_active = false;
	}
	++vr_input_commands_depth;
	(void)VR_InputCalibrationContactAdjustmentActive ();
	if (!VR_InputMotionContextAccepted (frame) || frame->reference_changed)
		vr_input_roomscale_position_valid = false;
	if (frame)
		memcpy (input_hands, frame->hands, sizeof (input_hands));
	context = VR_InputCurrentContext ();
	if (VR_WeaponCalibrationAdjustActive ())
	{
		vr_input_adjust_trigger_suppressed = true;
		if (!frame || !frame->focused || frame->reference_changed ||
			context.destination != key_game || context.input_grab)
			VR_WeaponCalibrationAdjustCancel ();
	}

	if (!vr_input_context_valid)
	{
		vr_input_context = context;
		vr_input_context_valid = true;
		VR_InputGateHand (0);
		VR_InputGateHand (1);
	}
	else if (!VR_InputSameContext (&vr_input_context, &context))
	{
		VR_InputInvalidateMotion ();
		vr_input_context = context;
		if (!VR_InputGateAndReleaseAll (dispatch_epoch))
			goto done;
		context = VR_InputCurrentContext ();
		if (!VR_InputSameContext (&vr_input_context, &context))
		{
			VR_InputAbortForContextChange (dispatch_epoch);
			goto done;
		}
	}

	if (!frame || !frame->focused)
	{
		VR_InputInvalidateMotion ();
		VR_InputGateAndReleaseAll (dispatch_epoch);
		goto done;
	}

	for (int hand = 0; hand < 2; ++hand)
	{
		vr_input_hand_state_t *state = &vr_input_hands[hand];
		const vrxr_input_t *input = &input_hands[hand];
		const int role = VR_InputRoleForPhysicalHand (hand);

		if (!state->identity_valid)
		{
			state->identity_valid = true;
			state->role = role;
			state->profile = input->profile;
			VR_InputGateHand (hand);
		}
		else if (state->role != role || state->profile != input->profile)
		{
			if (state->role != role)
				VR_InputInvalidateMotion ();
			state->role = role;
			state->profile = input->profile;
			if (!VR_InputGateAndReleaseHand (hand, dispatch_epoch))
				goto done;
			if (!VR_InputContextMatchesCurrent (&context))
			{
				VR_InputAbortForContextChange (dispatch_epoch);
				goto done;
			}
		}

		if (!input->active)
		{
			if (!VR_InputGateAndReleaseHand (hand, dispatch_epoch))
				goto done;
			if (!VR_InputContextMatchesCurrent (&context))
			{
				VR_InputAbortForContextChange (dispatch_epoch);
				goto done;
			}
			continue;
		}

		if (state->wait_neutral)
		{
			if (VR_InputNeutral (input))
				state->wait_neutral = false;
			continue;
		}

		if (context.input_grab || context.destination == key_game || context.destination == key_menu)
			VR_InputBuildHandDesired (desired, hand, input, &context,
				hand == dominant && vr_input_adjust_trigger_suppressed);
		else if (role == VR_INPUT_ROLE_LEFT && (input->pressed & (VRXR_BUTTON_SECONDARY | VRXR_BUTTON_MENU)))
			// Preserve native Escape navigation from the startup console/chat
			// without dispatching gameplay bindings into those destinations.
			VR_InputAddKey (desired, hand, K_ESCAPE);
	}

	if (VR_WeaponCalibrationAdjustActive ())
	{
		vec3_t live_origin, live_angles;
		const qboolean pose_valid = dominant >= 0 && dominant < 2 &&
			input_hands[dominant].active &&
			!vr_input_hands[dominant].wait_neutral &&
			V_TrackedPresentationHandWorldPose (dominant, live_origin, live_angles);
		VR_WeaponCalibrationAdjustInput (dominant,
			vr_input_hands[dominant].trigger_down, pose_valid,
			pose_valid ? live_origin : NULL, pose_valid ? live_angles : NULL);
	}

	if (context.input_grab)
	{
		/* Modal cancellation wins when a cancel key and confirm are sampled
		 * together, so the grab sees one unambiguous decision. */
		if (desired[0][K_ESCAPE] || desired[0][K_BBUTTON] ||
			desired[1][K_ESCAPE] || desired[1][K_BBUTTON])
			desired[0][K_ABUTTON] = desired[1][K_ABUTTON] = false;
	}

	VR_InputEmitDesired (desired, &context, dispatch_epoch);
	if (vr_input_adjust_trigger_suppressed && dominant >= 0 && dominant < 2 &&
		frame && frame->focused && input_hands[dominant].active &&
		VR_InputTriggerValue (&input_hands[dominant]) < 0.45f)
		vr_input_adjust_trigger_suppressed = false;

done:
	(void)VR_InputCalibrationContactAdjustmentActive ();
	--vr_input_commands_depth;
}

void VR_InputMenuPanelTrigger (const vrxr_frame_t *frame, qboolean panel_drawn)
{
	const unsigned int dispatch_epoch = vr_input_dispatch_epoch;
	qboolean desired[2][MAX_KEYS] = {{false}};
	vr_input_context_t context;
	vr_input_hand_state_t *state;
	const int dominant = VR_InputDominantPhysicalHand ();
	int hand;

	/* A menu action can enter SCR_ModalMessage, which redraws recursively from
	 * inside Key_Event. Only handle the post-draw phase after its command owner
	 * returns, and at most once for that input dispatch. */
	if (!dispatch_epoch || vr_input_commands_depth ||
		vr_input_menu_panel_dispatch_epoch == dispatch_epoch)
		return;
	vr_input_menu_panel_dispatch_epoch = dispatch_epoch;

	context = VR_InputCurrentContext ();
	if (!vr_input_context_valid)
	{
		VR_InputGateAndReleaseAll (dispatch_epoch);
		return;
	}
	if (!VR_InputSameContext (&vr_input_context, &context))
	{
		VR_InputAbortForContextChange (dispatch_epoch);
		return;
	}
	if (!frame || !frame->focused)
	{
		VR_InputInvalidateMotion ();
		VR_InputGateAndReleaseAll (dispatch_epoch);
		return;
	}
	if (context.destination != key_menu || context.binding_capture || context.input_grab)
		return;

	if (!VR_InputHandAccepted (frame, dominant))
	{
		VR_InputGateAndReleaseHand (dominant, dispatch_epoch);
		return;
	}

	state = &vr_input_hands[dominant];
	if (!state->trigger_down)
		return;
	if (!state->menu_trigger_key)
		state->menu_trigger_key = panel_drawn && M_VRPointerCanClick () ? K_MOUSE1 : K_ENTER;

	/* Carry the full pre-render ownership snapshot forward. The selected menu
	 * key remains held across frames and hover changes until trigger release. */
	for (hand = 0; hand < 2; ++hand)
		memcpy (desired[hand], vr_input_hands[hand].owned, sizeof (desired[hand]));
	VR_InputAddKey (desired, dominant, state->menu_trigger_key);
	VR_InputEmitDesired (desired, &context, dispatch_epoch);
}

static qboolean VR_InputSelectedTwinNailgun (qmodel_t **model_out,
	int *modelindex_out)
{
	const int modelindex = cl.stats[STAT_WEAPON];
	qmodel_t *model;

	if (modelindex < 1 || modelindex >= MAX_MODELS)
		return false;
	model = cl.model_precache[modelindex];
	if (!model || model != cl.viewent.model ||
		strcmp (model->name, "progs/v_tnailgun.mdl"))
		return false;
	if (model_out)
		*model_out = model;
	if (modelindex_out)
		*modelindex_out = modelindex;
	return true;
}

static qboolean VR_InputAkimboGameplayAccepted (const vrxr_frame_t *frame)
{
	return frame && frame->sample_id && frame->focused && frame->should_render &&
		cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		cl.vr_qbj3_akimbo_supported && V_AkimboPairReady () &&
		!VR_WeaponCalibrationAdjustActive () && VR_InputControllerAim () &&
		V_TrackedSessionActive () && !CL_AngleLocked () &&
		VR_InputMotionContextAccepted (frame) &&
		cls.state == ca_connected && cls.signon == SIGNONS &&
		!cls.demoplayback && !cl.intermission && !cl.paused &&
		key_dest == key_game && cl.stats[STAT_HEALTH] > 0 &&
		cl.worldmodel && !cl.worldmodel->needload && cl.entities &&
		cl.viewentity > 0 && cl.viewentity < cl.num_entities;
}

static qboolean VR_InputAkimboHandDevicesAccepted (
	const vrxr_frame_t *frame)
{
	int hand;

	for (hand = 0; hand < 2; ++hand)
	{
		const vrxr_device_t *device = &frame->devices[hand + 1];
		if (!VR_InputHandAccepted (frame, hand) || !device->valid ||
			!device->tracked || device->kind != VRXR_DEVICE_HAND ||
			device->hand != hand)
			return false;
	}
	return true;
}

static void VR_InputPrepareAkimboPair (usercmd_t *pending,
	const vrxr_frame_t *frame)
{
	qmodel_t *model;
	vec3_t muzzle[2], physical_angles[2];
	int modelindex, hand;
	const qboolean roomscale_accepted = pending &&
		VR_InputRoomscaleCommandAccepted (pending->vr_roomscalemove);

	VR_InputClearPendingAkimboRecord (pending);
	if (!pending || !pending->vr_active || !pending->vr_handpos_relative ||
		!VR_InputWireVec (pending->vr_handpos) ||
		!VR_InputAkimboGameplayAccepted (frame) ||
		!VR_InputSelectedTwinNailgun (&model, &modelindex) ||
		!VR_InputAkimboHandDevicesAccepted (frame))
		return;

	for (hand = 0; hand < 2; ++hand)
	{
		vec3_t grip, model_angles, local_anchor;
		if (!V_TrackedHandBodyOffset (hand, grip) ||
			!V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, hand,
				physical_angles[hand]) ||
			!VR_LocomotionHandRotToViewmodelAngles (physical_angles[hand],
				model_angles, vr_gunmodelpitch.value) ||
			!V_AkimboTransformAnchor (hand, model_angles, local_anchor))
			return;

		for (int axis = 0; axis < 3; ++axis)
			muzzle[hand][axis] = grip[axis] + local_anchor[axis] -
				(roomscale_accepted ? pending->vr_roomscalemove[axis] : 0.0f);
		if (!VR_InputWireVec (muzzle[hand]) ||
			!VR_InputWireVec (physical_angles[hand]))
			return;
	}

	memset (&vr_input_pending_akimbo_identity, 0,
		sizeof (vr_input_pending_akimbo_identity));
	vr_input_pending_akimbo_identity.sample_id = frame->sample_id;
	vr_input_pending_akimbo_identity.model = model;
	vr_input_pending_akimbo_identity.modelindex = modelindex;
	vr_input_pending_akimbo_identity.weapon = cl.stats[STAT_ACTIVEWEAPON];
	vr_input_pending_akimbo_identity.reset_generation =
		vr_input_reset_generation;
	for (hand = 0; hand < 2; ++hand)
	{
		const vrxr_device_t *device = &frame->devices[hand + 1];
		vr_input_pending_akimbo_identity.hands[hand].role =
			vr_input_hands[hand].role;
		vr_input_pending_akimbo_identity.hands[hand].profile =
			vr_input_hands[hand].profile;
		vr_input_pending_akimbo_identity.hands[hand].device_hand =
			device->hand;
		memcpy (vr_input_pending_akimbo_identity.hands[hand].serial,
			device->serial, sizeof (device->serial));
		VectorCopy (muzzle[hand], pending->vr_akimbo_muzzle[hand]);
		VectorCopy (physical_angles[hand], pending->vr_akimbo_angles[hand]);
	}
	pending->vr_akimbo_berserk = false;
	vr_input_pending_akimbo_identity.valid = true;
	pending->vr_akimbo_active = true;
}

static qboolean VR_InputPendingAkimboAccepted (const usercmd_t *pending,
	const vrxr_frame_t *frame)
{
	qmodel_t *model;
	int modelindex, hand;

	if (!pending || !pending->vr_akimbo_active || pending->vr_akimbo_berserk ||
		!vr_input_pending_akimbo_identity.valid ||
		!VR_InputAkimboGameplayAccepted (frame) ||
		!VR_InputSelectedTwinNailgun (&model, &modelindex) ||
		!pending->vr_active || !pending->vr_handpos_relative ||
		!VR_InputWireVec (pending->vr_handpos) ||
		vr_input_pending_akimbo_identity.sample_id != frame->sample_id ||
		vr_input_pending_akimbo_identity.reset_generation !=
			vr_input_reset_generation ||
		vr_input_pending_akimbo_identity.modelindex != modelindex ||
		vr_input_pending_akimbo_identity.model != model ||
		vr_input_pending_akimbo_identity.weapon != cl.stats[STAT_ACTIVEWEAPON] ||
		!VR_InputWireVec (pending->vr_akimbo_muzzle[0]) ||
		!VR_InputWireVec (pending->vr_akimbo_muzzle[1]) ||
		!VR_InputWireVec (pending->vr_akimbo_angles[0]) ||
		!VR_InputWireVec (pending->vr_akimbo_angles[1]) ||
		!VR_InputAkimboHandDevicesAccepted (frame))
		return false;

	for (hand = 0; hand < 2; ++hand)
	{
		const vrxr_device_t *device = &frame->devices[hand + 1];
		if (vr_input_pending_akimbo_identity.hands[hand].role !=
				vr_input_hands[hand].role ||
			vr_input_pending_akimbo_identity.hands[hand].profile !=
				vr_input_hands[hand].profile ||
			vr_input_pending_akimbo_identity.hands[hand].device_hand !=
				device->hand ||
			memcmp (vr_input_pending_akimbo_identity.hands[hand].serial,
				device->serial, sizeof (device->serial)) != 0)
			return false;
	}
	return true;
}

static void VR_InputPreparePrivatePose (usercmd_t *pending, int dominant,
	qboolean dominant_accepted)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	vec3_t grip, hand_angles, local_muzzle, world_muzzle, raw_world_muzzle, relative;
	qmodel_t *contact_model = NULL;
	qmodel_t *axe_model = NULL;
	aliashdr_t *axe_geometry = NULL;
	stockaxe_edge_t axe_edge;
	int contact_modelindex = 0;
	int axe_modelindex = 0, axe_skin = -1;
	qboolean axe_candidate;
	const qboolean roomscale_accepted =
		VR_InputRoomscaleCommandAccepted (pending->vr_roomscalemove);

	if (cl.protocol_qsvr != QSVR_PROTOCOL_PINNED || !VR_InputControllerAim () ||
		!dominant_accepted || dominant < 0 || dominant > 1 ||
		!V_TrackedHandBodyOffset (dominant, grip) ||
		!V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, dominant, hand_angles) ||
		!VR_WeaponCalibrationCurrentMuzzle (local_muzzle) ||
		!VR_LocomotionMuzzleOffsetToWorld (local_muzzle, hand_angles,
			vr_gunmodelscale.value, vr_gunmodelpitch.value,
			dominant == 0, world_muzzle))
		return;
	VectorCopy (world_muzzle, raw_world_muzzle);

	/* Exact source admission is separate from the shared calibrated command
	 * pose. Until a server offers MELEE + STOCK, native trigger firing remains
	 * on this same private-pose path. */
	axe_candidate = VR_InputMeleeAuthorized () &&
		VR_InputSelectedStockAxe (&axe_modelindex, &axe_model, &axe_skin,
			&axe_geometry, &axe_edge);

	/* Stock ranged aliases may use wall retraction; the exact stock axe keeps
	 * the raw calibrated muzzle and blade. Use STAT_WEAPON before the
	 * presentation viewent is refreshed. */
	{
		const qboolean collision_context = VR_WeaponCollisionAuthorized () &&
			cls.state == ca_connected && cls.signon == SIGNONS &&
			!cls.demoplayback && key_dest == key_game && !cl.intermission &&
			cl.stats[STAT_HEALTH] > 0 &&
			cl.stats[STAT_WEAPON] > 0 && cl.stats[STAT_WEAPON] < MAX_MODELS &&
			cl.model_precache[cl.stats[STAT_WEAPON]] &&
			VR_WeaponCalibrationStockRangedViewmodel (
				cl.model_precache[cl.stats[STAT_WEAPON]]->name) &&
			cl.worldmodel && !cl.worldmodel->needload && cl.entities &&
			cl.viewentity > 0 && cl.viewentity < cl.num_entities && frame &&
			frame->focused && frame->should_render &&
			frame->devices[0].valid && frame->devices[0].tracked &&
			frame->devices[dominant + 1].valid &&
			frame->devices[dominant + 1].tracked &&
			VR_InputHandAccepted (frame, dominant);
		if (collision_context)
		{
			vec3_t torso_offset, torso, world_grip, base, tip, delta;
			vec3_t corrected_muzzle;
			float head_height;
			entity_t *player = &cl.entities[cl.viewentity];
			contact_modelindex = cl.stats[STAT_WEAPON];
			contact_model = cl.model_precache[contact_modelindex];
			if (R_TrackedHeadBodyOffset (torso_offset) &&
				R_TrackedHeadEyeHeight (cl.stats[STAT_VIEWHEIGHT], &head_height))
			{
				VectorAdd (player->origin, torso_offset, torso);
				torso[2] += head_height;
				VectorAdd (player->origin, grip, world_grip);
				VectorCopy (world_grip, base);
				VectorAdd (world_grip, world_muzzle, tip);
				if (CL_ResolveWeaponCollision (torso, world_grip, base, tip, delta))
				{
					/* Correct before applying the existing roomscale offset once. */
					VectorAdd (world_muzzle, delta, corrected_muzzle);
					VectorCopy (corrected_muzzle, world_muzzle);
				}
			}
		}
	}

	for (int i = 0; i < 3; ++i)
		relative[i] = grip[i] + world_muzzle[i] -
			(roomscale_accepted ? pending->vr_roomscalemove[i] : 0.0f);
	if (!VR_InputWireVec (relative) ||
		!isfinite (hand_angles[0]) || !isfinite (hand_angles[1]) ||
		!isfinite (hand_angles[2]))
		return;

	VectorCopy (relative, pending->vr_handpos);
	VectorCopy (hand_angles, pending->vr_handrot);
	pending->vr_handpos_relative = true;
	pending->vr_active = true;
	if (VR_WeaponCalibrationAdjustActive ())
	{
		VR_InputClearPendingContactRecord (pending);
		return;
	}
	VR_InputPrepareAkimboPair (pending, frame);
	if ((contact_model || axe_candidate) && vr_input_contact_discontinuity)
	{
		/* An inactive contact in this accepted command resets the server's
		 * previous pose; the next physical sample starts a new sweep. */
		vr_input_contact_discontinuity = false;
		return;
	}
	if (axe_candidate)
		VR_InputPrepareMeleeContact (pending, frame, dominant, grip,
			axe_modelindex, axe_model, axe_skin, axe_geometry, &axe_edge);
	else if (contact_model)
		VR_InputPrepareCollisionContact (pending, GL_OpenXRFrame (), dominant,
			grip, raw_world_muzzle, contact_modelindex, contact_model);
}

qboolean VR_InputCrosshairAimRay (vec3_t start, vec3_t forward)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const int dominant = VR_InputDominantPhysicalHand ();
	vec3_t body_offset, hand_angles, local_muzzle, world_muzzle, right, up;
	vec3_t collision_origin, collision_offset;
	entity_t *view_player;

	if (!start || !forward || !frame || !frame->should_render ||
		!VR_InputControllerAim () || cls.state != ca_connected ||
		cls.signon != SIGNONS || cls.demoplayback || cl.intermission ||
		!cl.entities || cl.viewentity <= 0 || cl.viewentity >= cl.num_entities ||
		!V_TrackedPresentationHandBodyOffset (dominant, body_offset) ||
		!V_TrackedPresentationHandAngles (dominant, hand_angles) ||
		!VR_WeaponCalibrationCurrentMuzzle (local_muzzle) ||
		!VR_LocomotionMuzzleOffsetToWorld (local_muzzle, hand_angles,
			vr_gunmodelscale.value, vr_gunmodelpitch.value, dominant == 0,
			world_muzzle))
		return false;

	view_player = &cl.entities[cl.viewentity];
	if (V_TrackedWeaponCollisionPresentation (collision_origin, collision_offset))
	{
		/* Shared with the held model; this origin already includes the stair
		 * presentation rebase and the single collision translation. */
		VectorCopy (collision_origin, start);
	}
	else
		VectorAdd (view_player->origin, body_offset, start);
	VectorAdd (start, world_muzzle, start);
	AngleVectors (hand_angles, forward, right, up);
	for (int i = 0; i < 3; ++i)
		if (!isfinite (start[i]) || !isfinite (forward[i]))
			return false;
	return true;
}

void VR_InputMove (usercmd_t *pending)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const qboolean turn180 = vr_input_turn180_queued;
	const int mode = VR_InputMovementMode ();
	const int offhand = VR_InputPhysicalHandForRole (VR_INPUT_ROLE_LEFT);
	const int dominant = 1 - offhand;
	vrxr_input_t offhand_input, dominant_input;
	vec3_t selected_angles, command_angles, contribution;
	qboolean offhand_accepted, dominant_accepted, selected_valid = false;
	qboolean move_armed, turn_armed, mapping_valid, command_valid;
	const qboolean controller_aim = VR_InputControllerAim ();
	vec3_t mapped_head;

	(void)VR_InputCalibrationContactAdjustmentActive ();
	vr_input_turn180_queued = false;
	VR_InputClearPendingRecord (pending);
	if (!pending)
		return;
	VR_InputAccumulateRoomscaleMove (frame, pending);

	if (!VR_InputMotionContextAccepted (frame) || CL_AngleLocked ())
	{
		VR_InputGateMovement (pending);
		VR_InputGateTurn ();
		vr_input_gorilla_discontinuity = true;
		return;
	}

	memcpy (&offhand_input, &frame->hands[offhand], sizeof (offhand_input));
	memcpy (&dominant_input, &frame->hands[dominant], sizeof (dominant_input));
	offhand_accepted = VR_InputHandAccepted (frame, offhand) && VR_InputAxesFinite (&offhand_input);
	dominant_accepted = VR_InputHandAccepted (frame, dominant) && VR_InputAxesFinite (&dominant_input);

	// Tracking must be usable before neutral can rearm a motion channel.
	// Button navigation remains independent of grip/head pose loss.
	mapping_valid = V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HEAD, offhand, mapped_head);
	selected_valid = mapping_valid && V_TrackedMovementAngles (mode, offhand, selected_angles);
	command_valid = selected_valid;
	if (controller_aim && mode == VR_MOVEMENT_MODE_RAW_INPUT)
		command_valid = mapping_valid && V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, dominant, command_angles);
	if (!offhand_accepted || !selected_valid || (controller_aim && !command_valid))
		VR_InputGateMovement (pending);
	if (!dominant_accepted || !mapping_valid)
		VR_InputGateTurn ();

	move_armed = offhand_accepted && selected_valid && (!controller_aim || command_valid) && !vr_input_move_wait_neutral;
	if (offhand_accepted && selected_valid && (!controller_aim || command_valid) &&
		vr_input_move_wait_neutral && VR_InputNeutral (&offhand_input))
		vr_input_move_wait_neutral = false;

	turn_armed = dominant_accepted && mapping_valid && !vr_input_turn_wait_neutral;
	if (dominant_accepted && mapping_valid && vr_input_turn_wait_neutral && VR_InputNeutral (&dominant_input))
		vr_input_turn_wait_neutral = false;

	// A fresh native command may come from a keyboard binding; unlike stick
	// turning it does not require a controller axis to be available.
	if (turn180 && mapping_valid && VR_InputFiniteCvar (&vr_180_snap_turn, 1.0f) != 0.0f)
	{
		if (!V_TurnTrackedYaw (-180.0f))
		{
			VR_InputGateTurn ();
			turn_armed = false;
		}
		else
		{
			vr_input_contact_discontinuity = true;
			vr_input_gorilla_discontinuity = true;
		}
	}

	if (turn_armed)
	{
		const float yaw_move = VR_InputFilteredAxis (&dominant_input, 0, 0.0f);
		const float snap_turn = VR_InputFiniteCvar (&vr_snap_turn, 0.0f);

		if (snap_turn != 0.0f)
		{
			const int snap = yaw_move > 0.0f ? 1 : yaw_move < 0.0f ? -1 : 0;
			if (snap != vr_input_last_snap)
			{
				if (snap && !V_TurnTrackedYaw (-snap * snap_turn))
				{
					VR_InputGateTurn ();
					turn_armed = false;
				}
				else
				{
					if (snap)
					{
						vr_input_contact_discontinuity = true;
						vr_input_gorilla_discontinuity = true;
					}
					vr_input_last_snap = snap;
				}
			}
		}
		else if (yaw_move != 0.0f)
		{
			const double delta = -(double)yaw_move * host_frametime * 100.0 *
				(double)VR_InputFiniteCvar (&vr_joystick_yaw_multi, 1.0f) *
				(double)VR_InputFiniteCvar (&vr_turn_speed, 2.0f);
			if (!isfinite (delta) || !V_TurnTrackedYaw ((float)delta))
			{
				VR_InputGateTurn ();
				turn_armed = false;
			}
		}
	}

	/* Capture mapped movement/command orientation only after local turning so
	 * this prepared command has one consistent post-turn basis. */
	selected_valid = mapping_valid && V_TrackedMovementAngles (mode, offhand, selected_angles);
	if (!selected_valid)
	{
		VR_InputGateMovement (pending);
		move_armed = false;
	}

	if (move_armed && selected_valid)
	{
		const float forward_axis = VR_InputFilteredAxis (&offhand_input, 1, 0.0f);
		const float side_axis = VR_InputFilteredAxis (&offhand_input, 0, 0.0f);
		float forward_speed = VR_InputFiniteCvar (&cl_forwardspeed, 200.0f);
		const float up_speed = VR_InputFiniteCvar (&cl_upspeed, 200.0f);
		const float movement_speed = VR_InputFiniteCvar (&vr_movement_speed, 1.0f);

		if (VR_InputFiniteCvar (&cl_desktop_vanilla_run, 1.0f) != 0.0f &&
			VR_InputFiniteCvar (&cl_alwaysrun, 1.0f) == 0.0f && forward_speed == 200.0f)
			forward_speed *= VR_InputFiniteCvar (&cl_movespeedkey, 2.0f);

		if (VR_LocomotionMove (mode, selected_angles, selected_angles,
			forward_axis, side_axis, forward_speed, up_speed, contribution))
		{
			for (int component = 0; component < 3; ++component)
				contribution[component] *= movement_speed;
			if ((in_speed.state & 1) ^ (VR_InputFiniteCvar (&cl_alwaysrun, 1.0f) != 0.0f))
				for (int component = 0; component < 3; ++component)
					contribution[component] *= VR_InputFiniteCvar (&cl_movespeedkey, 2.0f);
			if (VR_InputWireVec (contribution))
			{
				VectorCopy (contribution, pending->vr_pending_move);
				pending->vr_pending_move_valid = true;
			}
		}
	}

	if (controller_aim)
	{
		command_valid = false;
		if (mode == VR_MOVEMENT_MODE_RAW_INPUT)
		{
			command_valid = mapping_valid && V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, dominant, command_angles);
		}
		else if (selected_valid)
		{
			VectorCopy (selected_angles, command_angles);
			command_valid = true;
		}

		if (command_valid && isfinite (command_angles[PITCH]) && isfinite (command_angles[YAW]))
		{
			command_angles[PITCH] = fmodf (command_angles[PITCH], 360.f);
			command_angles[YAW] = fmodf (command_angles[YAW], 360.f);
			command_angles[ROLL] = 0.0f;
			VectorCopy (command_angles, pending->vr_pending_angles);
			pending->vr_pending_angles_valid = true;
		}
	}
	VR_InputPrepareGorillaSample (pending, frame);
	VR_InputPreparePrivatePose (pending, dominant, dominant_accepted);
}

void VR_InputApplyPending (usercmd_t *cmd)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	qboolean private_pose_accepted = false;
	vec3_t merged;

	(void)VR_InputCalibrationContactAdjustmentActive ();
	if (!cmd)
	{
		VR_InputClearPendingContactRecord (&cl.pendingcmd);
		VR_InputClearPendingAkimboRecord (&cl.pendingcmd);
		return;
	}
	memset (&cmd->vr_contact, 0, sizeof (cmd->vr_contact));
	cmd->vr_akimbo_active = false;
	cmd->vr_akimbo_berserk = false;
	memset (cmd->vr_akimbo_muzzle, 0, sizeof (cmd->vr_akimbo_muzzle));
	memset (cmd->vr_akimbo_angles, 0, sizeof (cmd->vr_akimbo_angles));
	if (CL_AngleLocked () || !VR_InputMotionContextAccepted (frame))
	{
		VR_InputClearPendingContactRecord (&cl.pendingcmd);
		VR_InputClearPendingAkimboRecord (&cl.pendingcmd);
		return;
	}
	if (VR_InputControllerAim () && VR_InputRoomscaleCommandAccepted (cl.pendingcmd.vr_roomscalemove))
		VectorCopy (cl.pendingcmd.vr_roomscalemove, cmd->vr_roomscalemove);
	if (cl.protocol_qsvr == QSVR_PROTOCOL_PINNED && cl.pendingcmd.vr_active &&
		cl.pendingcmd.vr_handpos_relative &&
		VR_InputWireVec (cl.pendingcmd.vr_handpos) &&
		isfinite (cl.pendingcmd.vr_handrot[0]) &&
		isfinite (cl.pendingcmd.vr_handrot[1]) &&
		isfinite (cl.pendingcmd.vr_handrot[2]))
	{
		VectorCopy (cl.pendingcmd.vr_handpos, cmd->vr_handpos);
		VectorCopy (cl.pendingcmd.vr_handrot, cmd->vr_handrot);
		cmd->vr_handpos_relative = true;
		cmd->vr_active = true;
		private_pose_accepted = true;
	}
	if (private_pose_accepted &&
		VR_InputPendingAkimboAccepted (&cl.pendingcmd, frame))
	{
		cmd->vr_akimbo_active = true;
		cmd->vr_akimbo_berserk = false;
		memcpy (cmd->vr_akimbo_muzzle, cl.pendingcmd.vr_akimbo_muzzle,
			sizeof (cmd->vr_akimbo_muzzle));
		memcpy (cmd->vr_akimbo_angles, cl.pendingcmd.vr_akimbo_angles,
			sizeof (cmd->vr_akimbo_angles));
	}
	else
		VR_InputClearPendingAkimboRecord (&cl.pendingcmd);
	if (private_pose_accepted &&
		VR_InputPendingContactAccepted (&cl.pendingcmd, frame))
		cmd->vr_contact = cl.pendingcmd.vr_contact;
	else
		VR_InputClearPendingContactRecord (&cl.pendingcmd);
	if (cl.pendingcmd.vr_gorilla.flags == VR_GORILLA_HANDS &&
		vr_gorilla.value && cl.vr_gorilla_supported && cl.vr_gorilla_allowed)
	{
		cmd->vr_gorilla = cl.pendingcmd.vr_gorilla;
		if (vr_input_gorilla_discontinuity)
			cmd->vr_gorilla.flags |= VR_GORILLA_RESET;
	}
	if (cl.pendingcmd.vr_pending_angles_valid &&
		isfinite (cl.pendingcmd.vr_pending_angles[PITCH]) &&
		isfinite (cl.pendingcmd.vr_pending_angles[YAW]) &&
		isfinite (cl.pendingcmd.vr_pending_angles[ROLL]))
		VectorCopy (cl.pendingcmd.vr_pending_angles, cmd->viewangles);

	if (!cl.pendingcmd.vr_pending_move_valid || !VR_InputWireVec (cl.pendingcmd.vr_pending_move))
		return;
	merged[0] = cmd->forwardmove + cl.pendingcmd.vr_pending_move[0];
	merged[1] = cmd->sidemove + cl.pendingcmd.vr_pending_move[1];
	merged[2] = cmd->upmove + cl.pendingcmd.vr_pending_move[2];
	if (!VR_InputWireVec (merged))
		return;
	cmd->forwardmove = merged[0];
	cmd->sidemove = merged[1];
	cmd->upmove = merged[2];
}

qboolean VR_InputSuppressUncalibratedAttack (const usercmd_t *cmd)
{
	/* A missing grip or weapon profile must not turn controller-aim firing
	 * into an ordinary body-origin shot. Dead players still need attack to
	 * request respawn through the normal button path. */
	return cmd && cl.protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		V_TrackedSessionActive () && VR_InputControllerAim () &&
		cl.stats[STAT_HEALTH] > 0 &&
		(!cmd->vr_active || !cmd->vr_handpos_relative);
}

void VR_InputCommitGorillaCommand (const usercmd_t *cmd)
{
	if (cmd && (cmd->vr_gorilla.flags & VR_GORILLA_RESET))
		vr_input_gorilla_discontinuity = false;
}

void VR_InputInvalidateMotion (void)
{
	VR_InputClearPendingRecord (&cl.pendingcmd);
	vr_input_contact_discontinuity = true;
	vr_input_gorilla_discontinuity = true;
	VectorCopy (vec3_origin, cl.pendingcmd.vr_roomscalemove);
	vr_input_roomscale_position_valid = false;
	vr_input_move_wait_neutral = true;
	vr_input_turn_wait_neutral = true;
	vr_input_last_snap = 0;
	vr_input_turn180_queued = false;
}

void VR_InputClear (void)
{
	VR_WeaponCalibrationAdjustCancel ();
	vr_input_adjust_trigger_suppressed = false;
	VR_InputFBTReset ();
	++vr_input_reset_generation;
	vr_input_roomscale_position_valid = false;
	memset (vr_input_emitted, 0, sizeof (vr_input_emitted));
	for (int hand = 0; hand < 2; ++hand)
	{
		memset (vr_input_hands[hand].owned, 0, sizeof (vr_input_hands[hand].owned));
		vr_input_hands[hand].trigger_down = false;
		vr_input_hands[hand].menu_trigger_key = 0;
		vr_input_hands[hand].wait_neutral = true;
	}
	vr_input_menu_panel_dispatch_epoch = vr_input_dispatch_epoch;
	vr_input_context_valid = false;
	VR_InputInvalidateMotion ();
}
