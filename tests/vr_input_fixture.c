/* Exercise the production XR input adapter with native Key_Event recorded. */
#include "../Quake/vr_input.c"
#include "../Quake/menu.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
	int key;
	int down;
} recorded_event_t;

keydest_t key_dest = key_game;
enum m_state_e m_state = m_none;
static qboolean waiting_for_binding;
static qboolean input_grab_active;

static recorded_event_t events[256];
static int event_count;
static int registered_cvars;
static int escape_changes_context;
static int escape_reenters_clear;
static int start_binding_on_abutton;
static int change_menu_state_on_enter;
static vrxr_frame_t *mutate_frame_on_release;

void Cvar_RegisterVariable (cvar_t *var)
{
	var->value = strtof (var->string, NULL);
	++registered_cvars;
}

void Key_Event (int key, qboolean down)
{
	assert (event_count < (int)(sizeof (events) / sizeof (events[0])));
	events[event_count++] = (recorded_event_t){key, down};
	if (down && key == K_ABUTTON && start_binding_on_abutton)
	{
		waiting_for_binding = true;
		start_binding_on_abutton = 0;
	}
	if (!down && key == K_ABUTTON && mutate_frame_on_release)
	{
		mutate_frame_on_release->hands[1].pressed = 0;
		mutate_frame_on_release = NULL;
	}
	if (down && key == K_ESCAPE && escape_changes_context)
	{
		key_dest = key_menu;
		if (escape_reenters_clear)
			VR_InputClear ();
	}
	if (down && key == K_ENTER && change_menu_state_on_enter)
	{
		m_state = m_singleplayer;
		change_menu_state_on_enter = 0;
	}
}

qboolean M_WaitingForKeyBinding (void)
{
	return key_dest == key_menu && waiting_for_binding;
}

qboolean Key_InputGrabActive (void)
{
	return input_grab_active;
}

static void reset_events (void)
{
	event_count = 0;
	escape_changes_context = 0;
	escape_reenters_clear = 0;
	start_binding_on_abutton = 0;
	change_menu_state_on_enter = 0;
	mutate_frame_on_release = NULL;
}

static void expect_event (int index, int key, int down)
{
	assert (index < event_count);
	if (events[index].key != key || events[index].down != down)
		fprintf (stderr, "event %d: expected (%d,%d), got (%d,%d) of %d\n",
			index, key, down, events[index].key, events[index].down, event_count);
	assert (events[index].key == key);
	assert (events[index].down == down);
}

static vrxr_frame_t neutral_frame (void)
{
	vrxr_frame_t frame = {0};
	frame.focused = 1;
	for (int hand = 0; hand < 2; ++hand)
	{
		frame.hands[hand].active = 1;
		frame.hands[hand].profile = VRXR_PROFILE_TOUCH;
	}
	return frame;
}

static void arm_neutral (vrxr_frame_t *frame)
{
	VR_InputCommands (frame);
	assert (event_count == 0);
}

static void native_clear_then_neutral (vrxr_frame_t *frame)
{
	VR_InputClear ();
	reset_events ();
	arm_neutral (frame);
}

static void test_init_and_no_vr (void)
{
	registered_cvars = 0;
	VR_InputInit ();
	assert (registered_cvars == 5);
	assert (!strcmp (vr_lefthanded.name, "vr_lefthanded") && vr_lefthanded.value == 0.0f);
	assert (!strcmp (vr_joystick_axis_deadzone.name, "vr_joystick_axis_deadzone") && vr_joystick_axis_deadzone.value == 0.25f);
	assert (!strcmp (vr_joystick_axis_menu_deadzone_extra.name, "vr_joystick_axis_menu_deadzone_extra") && vr_joystick_axis_menu_deadzone_extra.value == 0.25f);
	assert (!strcmp (vr_joystick_axis_exponent.name, "vr_joystick_axis_exponent") && vr_joystick_axis_exponent.value == 1.0f);
	assert (!strcmp (vr_joystick_deadzone_trunc.name, "vr_joystick_deadzone_trunc") && vr_joystick_deadzone_trunc.value == 1.0f);
	reset_events ();
	VR_InputCommands (NULL);
	assert (event_count == 0);
}

static void test_lifecycle_and_hysteresis (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_game;
	vr_lefthanded.value = 0.0f;
	native_clear_then_neutral (&frame);

	frame.hands[0].pressed = VRXR_BUTTON_PRIMARY;
	frame.hands[1].pressed = VRXR_BUTTON_PRIMARY;
	VR_InputCommands (&frame);
	assert (event_count == 2);
	expect_event (0, K_ABUTTON, 1);
	expect_event (1, K_XBUTTON, 1);

	reset_events ();
	frame.hands[0].active = 0;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ABUTTON, 0);
	frame.hands[0].active = 1;
	frame.hands[0].pressed = 0;
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	assert (event_count == 2);
	expect_event (1, K_XBUTTON, 0);

	native_clear_then_neutral (&frame);
	frame.hands[0].trigger = 0.56f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_LTRIGGER, 1);
	frame.hands[0].trigger = 0.50f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	frame.hands[0].trigger = 0.44f;
	VR_InputCommands (&frame);
	assert (event_count == 2);
	expect_event (1, K_LTRIGGER, 0);

	reset_events ();
	frame.hands[0].profile = VRXR_PROFILE_SIMPLE;
	frame.hands[0].trigger = 0.0f;
	frame.hands[0].pressed = VRXR_BUTTON_TRIGGER;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* profile change gates held input */
	frame.hands[0].pressed = 0;
	VR_InputCommands (&frame);
	frame.hands[0].pressed = VRXR_BUTTON_TRIGGER;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_LTRIGGER, 1);
}

static void test_role_profile_and_duplicate_mapping (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_game;
	vr_lefthanded.value = 0.0f;
	native_clear_then_neutral (&frame);

	/* Migration provenance: Touch/Index STICK is the donor legacy touchpad;
	 * Index PAD is donor Axis2. They are intentionally different bindings. */
	frame.hands[0].pressed = VRXR_BUTTON_STICK;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_LTHUMB, 1);
	reset_events ();
	frame.hands[0].pressed = 0;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_LTHUMB, 0);

	reset_events ();
	frame.hands[1].profile = VRXR_PROFILE_INDEX;
	frame.hands[1].pressed = VRXR_BUTTON_GRIP;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* profile change + held grip waits neutral */
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	frame.hands[1].pressed = VRXR_BUTTON_GRIP;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_RSHOULDER, 1);
	reset_events ();
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	expect_event (0, K_RSHOULDER, 0);
	frame.hands[1].pressed = VRXR_BUTTON_STICK;
	VR_InputCommands (&frame);
	expect_event (1, K_VR_ALTFIRE, 1);

	reset_events ();
	vr_lefthanded.value = 1.0f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_VR_ALTFIRE, 0);
	frame.hands[0].pressed = frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	reset_events ();
	frame.hands[0].pressed = VRXR_BUTTON_PRIMARY;
	frame.hands[1].pressed = VRXR_BUTTON_PRIMARY;
	VR_InputCommands (&frame);
	assert (event_count == 2);
	expect_event (0, K_ABUTTON, 1);
	expect_event (1, K_XBUTTON, 1);

	/* Both Index pads compose to the same logical Y key. Aggregate ownership
	 * keeps it down until the final contributing hand releases. */
	frame.hands[0].profile = VRXR_PROFILE_INDEX;
	frame.hands[1].profile = VRXR_PROFILE_INDEX;
	frame.hands[0].pressed = frame.hands[1].pressed = 0;
	VR_InputCommands (&frame); /* profile changes gate both hands */
	reset_events ();
	VR_InputCommands (&frame); /* neutral rearms */
	frame.hands[0].pressed = frame.hands[1].pressed = VRXR_BUTTON_PAD;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_YBUTTON, 1);
	reset_events ();
	frame.hands[0].pressed = 0;
	VR_InputCommands (&frame);
	assert (event_count == 0);
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_YBUTTON, 0);
}

static void test_button_profiles_and_menu_axes (void)
{
	vrxr_frame_t frame = neutral_frame ();
	vr_lefthanded.value = 0.0f;
	key_dest = key_game;
	native_clear_then_neutral (&frame);

	frame.hands[1].pressed = VRXR_BUTTON_SECONDARY;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_BBUTTON, 1);
	reset_events ();
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	expect_event (0, K_BBUTTON, 0);

	frame.hands[1].profile = VRXR_PROFILE_FRAME;
	frame.hands[1].pressed = VRXR_BUTTON_GRIP;
	reset_events ();
	VR_InputCommands (&frame);
	assert (event_count == 0);
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	frame.hands[1].pressed = VRXR_BUTTON_GRIP;
	VR_InputCommands (&frame);
	expect_event (0, K_VR_ALTFIRE, 1);
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);

	key_dest = key_menu;
	frame = neutral_frame ();
	frame.hands[1].profile = VRXR_PROFILE_FRAME;
	reset_events ();
	VR_InputCommands (&frame); /* context change gates */
	reset_events ();
	frame.hands[0].pad[0] = 0.70f; /* strongest finite source wins even on Touch */
	frame.hands[1].stick[1] = -0.80f;
	frame.hands[0].trigger = 0.60f;
	frame.hands[1].trigger = 0.90f;
	VR_InputCommands (&frame);
	assert (event_count == 4);
	expect_event (0, K_ENTER, 1);
	expect_event (1, K_RIGHTARROW, 1);
	expect_event (2, K_LTRIGGER, 1);
	expect_event (3, K_VR_RIGHT_STICK_DOWN, 1);

	reset_events ();
	frame.hands[0].pad[0] = 0.40f;
	frame.hands[1].stick[1] = 0.0f;
	frame.hands[0].trigger = 0.0f;
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	assert (event_count == 4);
	expect_event (0, K_ENTER, 0);
	expect_event (1, K_RIGHTARROW, 0);
	expect_event (2, K_LTRIGGER, 0);
	expect_event (3, K_VR_RIGHT_STICK_DOWN, 0);
}

static void test_context_reentry_clear_and_nan (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_game;
	vr_lefthanded.value = 0.0f;
	native_clear_then_neutral (&frame);

	frame.hands[0].pressed = VRXR_BUTTON_MENU;
	frame.hands[1].trigger = 0.8f;
	escape_changes_context = 1;
	escape_reenters_clear = 1;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ESCAPE, 1);
	assert (key_dest == key_menu);

	reset_events ();
	VR_InputCommands (&frame);
	assert (event_count == 0); /* held controls cannot rearm after recursive clear */
	frame.hands[0].pressed = 0;
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	reset_events ();
	frame.hands[0].pressed = VRXR_BUTTON_PRIMARY;
	VR_InputCommands (&frame);
	expect_event (0, K_ABUTTON, 1);

	reset_events ();
	VR_InputClear ();
	assert (event_count == 0); /* clear never emits */
	frame.hands[0].pressed = VRXR_BUTTON_PRIMARY;
	VR_InputCommands (&frame);
	assert (event_count == 0);
	frame.hands[0].pressed = 0;
	frame.hands[0].stick[0] = NAN;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* malformed axes cannot satisfy neutral */
	frame.hands[0].stick[0] = 0.0f;
	VR_InputCommands (&frame);
	frame.hands[0].pressed = VRXR_BUTTON_PRIMARY;
	VR_InputCommands (&frame);
	assert (event_count == 1);

	reset_events ();
	key_dest = key_console;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ABUTTON, 0);
	reset_events ();
	VR_InputCommands (&frame);
	assert (event_count == 0);
	key_dest = key_message;
	VR_InputCommands (&frame);
	assert (event_count == 0);
}

static void test_axis_threshold_edges_and_console_escape (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_game;
	vr_lefthanded.value = 0;
	vr_joystick_axis_deadzone.value = 0;
	vr_joystick_axis_menu_deadzone_extra.value = 0;
	vr_joystick_axis_exponent.value = 0;
	native_clear_then_neutral (&frame);
	VR_InputCommands (&frame);
	assert (event_count == 0); /* pow(0,0) must not manufacture a direction. */
	vr_joystick_axis_deadzone.value = .75f;
	vr_joystick_axis_menu_deadzone_extra.value = .5f;
	vr_joystick_axis_exponent.value = 1;
	frame.hands[1].stick[1] = 1;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* Configured combined threshold exceeds full scale. */
	vr_joystick_axis_deadzone.value = .25f;
	vr_joystick_axis_menu_deadzone_extra.value = .25f;
	frame = neutral_frame ();
	key_dest = key_console;
	native_clear_then_neutral (&frame);
	frame.hands[0].pressed = VRXR_BUTTON_MENU;
	frame.hands[1].trigger = 1;
	escape_changes_context = 1;
	VR_InputCommands (&frame);
	assert (event_count == 2);
	expect_event (0, K_ESCAPE, 1);
	expect_event (1, K_ESCAPE, 0);
	assert (key_dest == key_menu);
}

static void test_menu_trigger_dispatch_and_capture_transition (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_menu;
	waiting_for_binding = false;
	input_grab_active = false;
	native_clear_then_neutral (&frame);

	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 1);
	reset_events ();
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 0);

	reset_events ();
	waiting_for_binding = true;
	VR_InputCommands (&frame); /* capture-state transition gates until neutral */
	assert (event_count == 0);
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_RTRIGGER, 1);
}

static void test_capture_change_stops_same_batch_presses (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_menu;
	waiting_for_binding = false;
	input_grab_active = false;
	native_clear_then_neutral (&frame);

	frame.hands[0].pressed = VRXR_BUTTON_PRIMARY;
	frame.hands[1].pressed = VRXR_BUTTON_PRIMARY;
	start_binding_on_abutton = 1;
	VR_InputCommands (&frame);
	assert (event_count == 2);
	expect_event (0, K_ABUTTON, 1);
	expect_event (1, K_ABUTTON, 0);
	assert (waiting_for_binding);
}

static void test_modal_grab_overrides_destination_and_rearms (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_console;
	waiting_for_binding = false;
	input_grab_active = true;
	native_clear_then_neutral (&frame);

	frame.hands[1].pressed = VRXR_BUTTON_SECONDARY;
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_BBUTTON, 1);

	key_dest = key_game;
	frame = neutral_frame ();
	input_grab_active = true;
	native_clear_then_neutral (&frame);
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ABUTTON, 1);

	reset_events ();
	input_grab_active = false;
	VR_InputCommands (&frame);
	assert (event_count == 1); /* modal release, no stale RTRIGGER press */
	expect_event (0, K_ABUTTON, 0);
	reset_events ();
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	assert (event_count == 0);
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_RTRIGGER, 1);
}

static void test_menu_state_change_stops_same_batch_presses (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_menu;
	m_state = m_main;
	waiting_for_binding = false;
	input_grab_active = false;
	native_clear_then_neutral (&frame);

	frame.hands[0].pressed = VRXR_BUTTON_PRIMARY;
	frame.hands[1].trigger = 0.60f;
	change_menu_state_on_enter = 1;
	VR_InputCommands (&frame);
	assert (event_count == 2);
	expect_event (0, K_ENTER, 1);
	expect_event (1, K_ENTER, 0);
	assert (m_state == m_singleplayer);
}

static void test_modal_grab_only_emits_decision_keys (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_menu;
	m_state = m_main;
	waiting_for_binding = false;
	input_grab_active = true;
	native_clear_then_neutral (&frame);

	frame.hands[1].pressed = VRXR_BUTTON_PRIMARY;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* right primary is unrelated during a modal grab */

	frame.hands[0].stick[0] = 0.9f;
	frame.hands[0].stick[1] = -0.9f;
	frame.hands[1].pressed |= VRXR_BUTTON_GRIP | VRXR_BUTTON_STICK | VRXR_BUTTON_PAD;
	frame.hands[1].stick[1] = 0.9f;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* unrelated buttons and axes are ignored */

	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ABUTTON, 1);
}

static void test_modal_cancel_wins_over_confirm (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_console;
	m_state = m_none;
	waiting_for_binding = false;
	input_grab_active = true;
	native_clear_then_neutral (&frame);

	frame.hands[0].pressed = VRXR_BUTTON_MENU | VRXR_BUTTON_PRIMARY;
	frame.hands[1].pressed = VRXR_BUTTON_GRIP | VRXR_BUTTON_STICK;
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ESCAPE, 1);
}

static void test_input_hands_are_snapshotted_before_release_callbacks (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_game;
	waiting_for_binding = false;
	input_grab_active = false;
	native_clear_then_neutral (&frame);

	frame.hands[0].pressed = VRXR_BUTTON_PRIMARY;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ABUTTON, 1);

	reset_events ();
	frame.hands[0].profile = VRXR_PROFILE_FRAME;
	frame.hands[0].pressed = 0;
	frame.hands[1].pressed = VRXR_BUTTON_PRIMARY;
	mutate_frame_on_release = &frame;
	VR_InputCommands (&frame);
	assert (event_count == 2);
	expect_event (0, K_ABUTTON, 0);
	expect_event (1, K_XBUTTON, 1);
	assert (frame.hands[1].pressed == 0); /* callback mutation did not alter this sample */
}

int main (void)
{
	test_init_and_no_vr ();
	test_lifecycle_and_hysteresis ();
	test_role_profile_and_duplicate_mapping ();
	test_button_profiles_and_menu_axes ();
	test_context_reentry_clear_and_nan ();
	test_axis_threshold_edges_and_console_escape ();
	test_menu_trigger_dispatch_and_capture_transition ();
	test_capture_change_stops_same_batch_presses ();
	test_modal_grab_overrides_destination_and_rearms ();
	test_menu_state_change_stops_same_batch_presses ();
	test_modal_grab_only_emits_decision_keys ();
	test_modal_cancel_wins_over_confirm ();
	test_input_hands_are_snapshotted_before_release_callbacks ();
	puts ("VR input adapter preserves native key ownership, menu dispatch, gating and re-entry safety");
	return 0;
}
