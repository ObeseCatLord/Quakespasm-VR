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
#include "vr_input.h"

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

static vr_input_hand_state_t vr_input_hands[2];
static qboolean vr_input_emitted[MAX_KEYS];
static qboolean vr_input_context_valid;
static vr_input_context_t vr_input_context;
static unsigned int vr_input_reset_generation;

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
}
