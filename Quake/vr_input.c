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
#include "vr_input.h"
#include "vr_locomotion.h"

#include <math.h>
#include <string.h>

enum
{
	VR_ROLE_LEFT,
	VR_ROLE_RIGHT
};

typedef struct
{
	qboolean owned[MAX_KEYS];
	qboolean wait_neutral;
	qboolean trigger_down;
	qboolean identity_valid;
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

extern cvar_t vr_aimmode;

static vr_input_hand_state_t vr_input_hands[2];
static qboolean vr_input_emitted[MAX_KEYS];
static qboolean vr_input_context_valid;
static vr_input_context_t vr_input_context;
static unsigned int vr_input_reset_generation;
static qboolean vr_input_move_wait_neutral;
static qboolean vr_input_turn_wait_neutral;
static int vr_input_last_snap;
static qboolean vr_input_turn180_queued;

#define VR_INPUT_WIRE_MIN (-32768.0f)
#define VR_INPUT_WIRE_MAX 32767.0f

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

static qboolean VR_InputReleaseHand (int hand)
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
		if (generation != vr_input_reset_generation)
			return false;
	}
	return true;
}

static qboolean VR_InputReleaseAll (void)
{
	return VR_InputReleaseHand (0) && VR_InputReleaseHand (1);
}

static void VR_InputGateHand (int hand)
{
	vr_input_hands[hand].wait_neutral = true;
	vr_input_hands[hand].trigger_down = false;
	if (VR_InputRoleForPhysicalHand (hand) == VR_ROLE_LEFT)
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

static qboolean VR_InputGateAndReleaseHand (int hand)
{
	VR_InputGateHand (hand);
	return VR_InputReleaseHand (hand);
}

static qboolean VR_InputGateAndReleaseAll (void)
{
	VR_InputGateHand (0);
	VR_InputGateHand (1);
	return VR_InputReleaseAll ();
}

/* A native callback can change the destination, binding-capture mode, or
 * modal grab while a batch is being dispatched. Release every key already
 * emitted in that batch and leave both hands neutral-gated. */
static qboolean VR_InputAbortForContextChange (void)
{
	vr_input_context = VR_InputCurrentContext ();
	vr_input_context_valid = true;
	if (!VR_InputGateAndReleaseAll ())
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
		state->trigger_down = false;
}

static void VR_InputBuildHandDesired (qboolean desired[2][MAX_KEYS], int hand,
	const vrxr_input_t *input, const vr_input_context_t *context)
{
	vr_input_hand_state_t *state = &vr_input_hands[hand];
	const qboolean logical_left = state->role == VR_ROLE_LEFT;
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
			trigger_key = context->binding_capture ? K_RTRIGGER : K_ENTER;
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
	const vr_input_context_t *expected_context)
{
	for (int phase = 0; phase < 2; ++phase)
	{
		for (int key = 0; key < MAX_KEYS; ++key)
		{
			const qboolean desired_aggregate = desired[0][key] || desired[1][key];
			const qboolean current_aggregate = vr_input_emitted[key];
			unsigned int generation;

			if ((phase == 0 && (desired_aggregate || !current_aggregate)) ||
				(phase == 1 && (!desired_aggregate || current_aggregate)))
				continue;

			vr_input_hands[0].owned[key] = desired[0][key];
			vr_input_hands[1].owned[key] = desired[1][key];
			vr_input_emitted[key] = desired_aggregate;
			generation = vr_input_reset_generation;
			Key_Event (key, desired_aggregate);

			if (generation != vr_input_reset_generation)
				return false;
			if (!VR_InputContextMatchesCurrent (expected_context))
				return VR_InputAbortForContextChange ();
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
	Cvar_RegisterVariable (&vr_lefthanded);
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
	Cvar_SetCallback (&vr_lefthanded, VR_InputMotionSettingsChanged);
	Cvar_SetCallback (&vr_movement_mode, VR_InputMotionSettingsChanged);
	Cvar_SetCallback (&vr_snap_turn, VR_InputMotionSettingsChanged);
	Cmd_AddCommand ("vr_turn180", VR_InputTurn180_f);
	VR_InputClear ();
}

void VR_InputCommands (const vrxr_frame_t *frame)
{
	qboolean desired[2][MAX_KEYS] = {{false}};
	vrxr_input_t input_hands[2];
	vr_input_context_t context;
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
		if (!VR_InputGateAndReleaseAll ())
			return;
		context = VR_InputCurrentContext ();
		if (!VR_InputSameContext (&vr_input_context, &context))
		{
			VR_InputAbortForContextChange ();
			return;
		}
	}

	if (!frame || !frame->focused)
	{
		VR_InputInvalidateMotion ();
		VR_InputGateAndReleaseAll ();
		return;
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
			if (!VR_InputGateAndReleaseHand (hand))
				return;
			if (!VR_InputContextMatchesCurrent (&context))
			{
				VR_InputAbortForContextChange ();
				return;
			}
		}

		if (!input->active)
		{
			if (!VR_InputGateAndReleaseHand (hand))
				return;
			if (!VR_InputContextMatchesCurrent (&context))
			{
				VR_InputAbortForContextChange ();
				return;
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
		else if (role == VR_ROLE_LEFT && (input->pressed & (VRXR_BUTTON_SECONDARY | VRXR_BUTTON_MENU)))
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

	VR_InputEmitDesired (desired, &context);
}

void VR_InputMove (usercmd_t *pending)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const qboolean turn180 = vr_input_turn180_queued;
	const int mode = VR_InputMovementMode ();
	const int offhand = VR_InputPhysicalHandForRole (VR_ROLE_LEFT);
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
}

void VR_InputApplyPending (usercmd_t *cmd)
{
	vec3_t merged;

	if (!cmd || CL_AngleLocked () || !VR_InputMotionContextAccepted (GL_OpenXRFrame ()))
		return;
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

void VR_InputInvalidateMotion (void)
{
	VR_InputClearPendingRecord (&cl.pendingcmd);
	vr_input_move_wait_neutral = true;
	vr_input_turn_wait_neutral = true;
	vr_input_last_snap = 0;
	vr_input_turn180_queued = false;
}

void VR_InputClear (void)
{
	++vr_input_reset_generation;
	memset (vr_input_emitted, 0, sizeof (vr_input_emitted));
	for (int hand = 0; hand < 2; ++hand)
	{
		memset (vr_input_hands[hand].owned, 0, sizeof (vr_input_hands[hand].owned));
		vr_input_hands[hand].trigger_down = false;
		vr_input_hands[hand].wait_neutral = true;
	}
	vr_input_context_valid = false;
	VR_InputInvalidateMotion ();
}
