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
#include "vr_aim.h"
#include "vr_fbt.h"
#include "vr_input.h"
#include "vr_locomotion.h"
#include "vr_weapon_calibration.h"

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
static cvar_t vr_movement_speed = {"vr_movement_speed", "1", CVAR_ARCHIVE};
static cvar_t vr_snap_turn = {"vr_snap_turn", "0", CVAR_ARCHIVE};
static cvar_t vr_180_snap_turn = {"vr_180_snap_turn", "1", CVAR_ARCHIVE};
static cvar_t vr_turn_speed = {"vr_turn_speed", "2", CVAR_ARCHIVE};
static cvar_t vr_joystick_yaw_multi = {"vr_joystick_yaw_multi", "1", CVAR_ARCHIVE};
static cvar_t vr_vrik = {"vr_vrik", "1", CVAR_ARCHIVE};
cvar_t vr_fbt_enabled = {"vr_fbt_enabled", "0", CVAR_ARCHIVE};

extern cvar_t vr_aimmode;

static vr_input_hand_state_t vr_input_hands[2];
static qboolean vr_input_emitted[MAX_KEYS];
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
static vec3_t vr_input_roomscale_last_position;
static qboolean vr_input_roomscale_position_valid;

static vr_fbt_manager_t vr_input_fbt_manager;
static qboolean vr_input_fbt_initialized;
static uint64_t vr_input_fbt_snapshot_id;
static uint64_t vr_input_fbt_last_seen_sample_id;
static uint64_t vr_input_fbt_next_ephemeral_identity = 1;
static double vr_input_fbt_snapshot_time;
static qboolean vr_input_fbt_have_snapshot_time;
static struct
{
	uint64_t identity;
	qboolean connected;
} vr_input_fbt_slots[VRXR_MAX_DEVICES];

#define VR_INPUT_WIRE_MIN (-32768.0f)
#define VR_INPUT_WIRE_MAX 32767.0f
/* Keep producer samples within PM_VR_ROOMSCALE_MAX_DELTA in pmove.c. */
#define VR_INPUT_ROOM_SCALE_MAX_DELTA_UNITS 16.0f

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
}

static void VR_InputFBTEnabledChanged (cvar_t *var)
{
	(void)var;
	VR_InputFBTReset ();
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

static void VR_InputClearPendingRecord (usercmd_t *pending)
{
	if (!pending)
		return;
	VectorCopy (vec3_origin, pending->vr_handpos);
	VectorCopy (vec3_origin, pending->vr_handrot);
	pending->vr_handpos_relative = false;
	pending->vr_active = false;
	pending->vr_pending_move[0] = pending->vr_pending_move[1] = pending->vr_pending_move[2] = 0.0f;
	pending->vr_pending_angles[0] = pending->vr_pending_angles[1] = pending->vr_pending_angles[2] = 0.0f;
	pending->vr_pending_move_valid = false;
	pending->vr_pending_angles_valid = false;
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
	const vrxr_input_t *input, const vr_input_context_t *context)
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
		if (!logical_left && state->trigger_down)
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
	if (state->trigger_down)
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
	Cvar_RegisterVariable (&vr_movement_speed);
	Cvar_RegisterVariable (&vr_snap_turn);
	Cvar_RegisterVariable (&vr_180_snap_turn);
	Cvar_RegisterVariable (&vr_turn_speed);
	Cvar_RegisterVariable (&vr_joystick_yaw_multi);
	Cvar_RegisterVariable (&vr_vrik);
	Cvar_RegisterVariable (&vr_fbt_enabled);
	Cvar_SetCallback (&vr_lefthanded, VR_InputMotionSettingsChanged);
	Cvar_SetCallback (&vr_movement_mode, VR_InputMotionSettingsChanged);
	Cvar_SetCallback (&vr_snap_turn, VR_InputMotionSettingsChanged);
	Cvar_SetCallback (&vr_fbt_enabled, VR_InputFBTEnabledChanged);
	Cmd_AddCommand ("vr_turn180", VR_InputTurn180_f);
	Cmd_AddCommand ("vr_fbt_list", VR_InputFBTList_f);
	Cmd_AddCommand ("vr_fbt_assign", VR_InputFBTAssign_f);
	Cmd_AddCommand ("vr_fbt_unassign", VR_InputFBTUnassign_f);
	VR_InputClear ();
}

void VR_InputCommands (const vrxr_frame_t *frame)
{
	const unsigned int dispatch_epoch = ++vr_input_dispatch_epoch;
	qboolean desired[2][MAX_KEYS] = {{false}};
	vrxr_input_t input_hands[2];
	vr_input_context_t context;
	if (vr_fbt_enabled.value && frame && frame->reference_changed && frame->sample_id &&
		frame->sample_id != vr_input_fbt_last_seen_sample_id)
		VR_InputFBTReset ();
	VR_InputFBTReconcile (frame);
	if (cls.state != ca_connected)
	{
		cl.vrik_next_sequence = 0;
		cl.vrik_next_send_time = 0.0;
		cl.vrik_last_sent_active = false;
	}
	++vr_input_commands_depth;
	if (!VR_InputMotionContextAccepted (frame) || frame->reference_changed)
		vr_input_roomscale_position_valid = false;
	if (frame)
		memcpy (input_hands, frame->hands, sizeof (input_hands));
	context = VR_InputCurrentContext ();

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
			VR_InputBuildHandDesired (desired, hand, input, &context);
		else if (role == VR_INPUT_ROLE_LEFT && (input->pressed & (VRXR_BUTTON_SECONDARY | VRXR_BUTTON_MENU)))
			// Preserve native Escape navigation from the startup console/chat
			// without dispatching gameplay bindings into those destinations.
			VR_InputAddKey (desired, hand, K_ESCAPE);
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

done:
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

static void VR_InputPreparePrivatePose (usercmd_t *pending, int dominant,
	qboolean dominant_accepted)
{
	vec3_t grip, hand_angles, local_muzzle, world_muzzle, relative;
	const qboolean roomscale_accepted =
		VR_InputRoomscaleCommandAccepted (pending->vr_roomscalemove);

	if (cl.protocol_qsvr != QSVR_PROTOCOL_PINNED || !VR_InputControllerAim () ||
		!dominant_accepted ||
		!V_TrackedHandBodyOffset (dominant, grip) ||
		!V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, dominant, hand_angles) ||
		!VR_WeaponCalibrationCurrentMuzzle (local_muzzle) ||
		!VR_LocomotionMuzzleOffsetToWorld (local_muzzle, hand_angles,
			vr_gunmodelscale.value, vr_gunmodelpitch.value,
			dominant == 0, world_muzzle))
		return;

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
}

qboolean VR_InputCrosshairAimRay (vec3_t start, vec3_t forward)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const int dominant = VR_InputDominantPhysicalHand ();
	vec3_t body_offset, hand_angles, local_muzzle, world_muzzle, right, up;
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

	vr_input_turn180_queued = false;
	VR_InputClearPendingRecord (pending);
	if (!pending)
		return;
	VR_InputAccumulateRoomscaleMove (frame, pending);

	if (!VR_InputMotionContextAccepted (frame) || CL_AngleLocked ())
	{
		VR_InputGateMovement (pending);
		VR_InputGateTurn ();
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
		if (!V_TurnTrackedYaw (-180.0f))
		{
			VR_InputGateTurn ();
			turn_armed = false;
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
					vr_input_last_snap = snap;
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
	VR_InputPreparePrivatePose (pending, dominant, dominant_accepted);
}

void VR_InputApplyPending (usercmd_t *cmd)
{
	vec3_t merged;

	if (!cmd || CL_AngleLocked () || !VR_InputMotionContextAccepted (GL_OpenXRFrame ()))
		return;
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

void VR_InputInvalidateMotion (void)
{
	VR_InputClearPendingRecord (&cl.pendingcmd);
	VectorCopy (vec3_origin, cl.pendingcmd.vr_roomscalemove);
	vr_input_roomscale_position_valid = false;
	vr_input_move_wait_neutral = true;
	vr_input_turn_wait_neutral = true;
	vr_input_last_snap = 0;
	vr_input_turn180_queued = false;
}

void VR_InputClear (void)
{
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
