/* Exercise the production XR input adapter with native Key_Event recorded. */
#include "../Quake/quakedef.h"
#include "../Quake/menu.h"
#include "../Quake/vr_input.h"
#include "../Quake/vr_locomotion.h"
#include "../Quake/vr_weapon_calibration.h"
#include "../Quake/view.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
	int key;
	int down;
} recorded_event_t;

typedef struct
{
	int physical_hand;
	float duration_seconds;
	float amplitude;
} recorded_haptic_t;

keydest_t key_dest = key_game;
enum m_state_e m_state = m_none;
client_static_t cls;
client_state_t cl;
double host_frametime;
kbutton_t in_speed;
cvar_t cl_upspeed = {"cl_upspeed", "200", CVAR_NONE};
cvar_t cl_forwardspeed = {"cl_forwardspeed", "200", CVAR_ARCHIVE_GAME};
cvar_t cl_desktop_vanilla_run = {"cl_desktop_vanilla_run", "1", CVAR_ARCHIVE};
cvar_t cl_movespeedkey = {"cl_movespeedkey", "2", CVAR_NONE};
cvar_t cl_alwaysrun = {"cl_alwaysrun", "1", CVAR_ARCHIVE_GAME};
cvar_t vr_aimmode = {"vr_aimmode", "7", CVAR_ARCHIVE};
cvar_t vr_gunmodelpitch = {"vr_gunmodelpitch", "19", CVAR_ARCHIVE};
cvar_t vr_gunmodelscale = {"vr_gunmodelscale", "1.35", CVAR_ARCHIVE};

static qboolean waiting_for_binding;
static qboolean input_grab_active;
static qboolean angle_locked;
static const vrxr_frame_t *fixture_frame;
static vec3_t fixture_head_angles;
static vec3_t fixture_hand_angles[2];
static vec3_t fixture_raw_grip = {3.25f, -4.5f, 6.75f};
static vec3_t fixture_local_muzzle = {2.0f, -1.25f, 0.8f};
static float fixture_turn_yaw;
static float fixture_units_per_metre = 10.0f;
static int fixture_turn_calls;
static qboolean fixture_require_pose_identity;
static qboolean fixture_body_offset_available;
static qboolean fixture_muzzle_available;
static int fixture_body_offset_calls;
static int fixture_last_body_hand = -1;
static int fixture_muzzle_calls;

static recorded_event_t events[256];
static int event_count;
static recorded_haptic_t haptics[32];
static int haptic_count;
static int registered_cvars;
static cvar_t *registered_cvar[32];
static xcommand_t turn180_command;
static cmd_function_t registered_command;
static int escape_changes_context;
static int escape_reenters_clear;
static int start_binding_on_abutton;
static int change_menu_state_on_enter;
static vrxr_frame_t *mutate_frame_on_release;

void Cvar_RegisterVariable (cvar_t *var)
{
	var->value = strtof (var->string, NULL);
	assert (registered_cvars < (int)(sizeof (registered_cvar) / sizeof (registered_cvar[0])));
	registered_cvar[registered_cvars] = var;
	++registered_cvars;
}

void Cvar_SetCallback (cvar_t *var, cvarcallback_t callback)
{
	var->callback = callback;
}

cmd_function_t *Cmd_AddCommand2 (const char *name, xcommand_t function, cmd_source_t source, qboolean qcinterceptable)
{
	assert (!strcmp (name, "vr_turn180"));
	assert (source == src_command && !qcinterceptable);
	turn180_command = function;
	memset (&registered_command, 0, sizeof (registered_command));
	registered_command.name = name;
	registered_command.function = function;
	registered_command.srctype = source;
	return &registered_command;
}

static cvar_t *fixture_cvar (const char *name)
{
	for (int i = 0; i < registered_cvars; ++i)
		if (!strcmp (registered_cvar[i]->name, name))
			return registered_cvar[i];
	assert (!"fixture cvar not registered");
	return NULL;
}

static void set_cvar (const char *name, float value)
{
	cvar_t *var = fixture_cvar (name);
	var->value = value;
	if (var->callback)
		var->callback (var);
}

const vrxr_frame_t *GL_OpenXRFrame (void)
{
	return fixture_frame;
}

qboolean V_TrackedSessionActive (void)
{
	return fixture_frame != NULL;
}

void VRXR_Haptic (int physical_hand, float duration_seconds, float amplitude)
{
	assert (haptic_count < (int)(sizeof (haptics) / sizeof (haptics[0])));
	haptics[haptic_count++] = (recorded_haptic_t){physical_hand, duration_seconds, amplitude};
}

float V_VRUnitsPerMetre (void)
{
	return fixture_units_per_metre;
}

qboolean V_TrackedMappingYaw (float *yaw)
{
	if (!yaw || !fixture_frame || !fixture_frame->focused || !fixture_frame->devices[0].valid)
		return false;
	*yaw = fixture_turn_yaw;
	return true;
}

void Con_DPrintf (const char *fmt, ...)
{
	(void)fmt;
}

qboolean CL_AngleLocked (void)
{
	return angle_locked;
}

qboolean V_TrackedMovementAngles (int mode, int physical_offhand, vec3_t angles)
{
	const vec3_t *source;

	if (!fixture_frame || !fixture_frame->focused || !fixture_frame->devices[0].valid)
		return false;
	if (mode == VR_MOVEMENT_MODE_FOLLOW_HEAD)
		source = &fixture_head_angles;
	else
	{
		if (physical_offhand < 0 || physical_offhand > 1 ||
			!fixture_frame->devices[physical_offhand + 1].valid)
			return false;
		if (fixture_require_pose_identity &&
			(!fixture_frame->should_render ||
			 fixture_frame->devices[physical_offhand + 1].kind != VRXR_DEVICE_HAND ||
			 fixture_frame->devices[physical_offhand + 1].hand != physical_offhand))
			return false;
		source = &fixture_hand_angles[physical_offhand];
	}
	VectorCopy (*source, angles);
	angles[YAW] += fixture_turn_yaw;
	return true;
}

qboolean V_TrackedHandBodyOffset (int physical_hand, vec3_t out)
{
	const vrxr_device_t *hand;

	if (out)
		out[0] = out[1] = out[2] = 0.0f;
	if (!out || !fixture_body_offset_available || !fixture_frame ||
		!fixture_frame->focused || !fixture_frame->should_render ||
		!fixture_frame->devices[0].valid || physical_hand < 0 || physical_hand > 1)
		return false;
	hand = &fixture_frame->devices[physical_hand + 1];
	if (!hand->valid || hand->kind != VRXR_DEVICE_HAND || hand->hand != physical_hand)
		return false;

	VectorCopy (fixture_raw_grip, out);
	++fixture_body_offset_calls;
	fixture_last_body_hand = physical_hand;
	return true;
}

qboolean VR_WeaponCalibrationCurrentMuzzle (vec3_t out)
{
	if (!out)
		return false;
	out[0] = out[1] = out[2] = 0.0f;
	++fixture_muzzle_calls;
	if (!fixture_muzzle_available)
		return false;
	VectorCopy (fixture_local_muzzle, out);
	return true;
}

qboolean V_TurnTrackedYaw (float delta)
{
	if (!fixture_frame || !fixture_frame->focused || !fixture_frame->devices[0].valid ||
		angle_locked || !isfinite (delta) || !isfinite (fixture_turn_yaw + delta))
		return false;
	fixture_turn_yaw += delta;
	++fixture_turn_calls;
	return true;
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
	haptic_count = 0;
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

static void expect_haptic (int index, int physical_hand)
{
	assert (index < haptic_count);
	assert (haptics[index].physical_hand == physical_hand);
	assert (haptics[index].duration_seconds == 0.1f);
	assert (haptics[index].amplitude == 0.5f);
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
	memset (registered_cvar, 0, sizeof (registered_cvar));
	turn180_command = NULL;
	VR_InputInit ();
	assert (registered_cvars == 12);
	assert (fixture_cvar ("vr_lefthanded")->value == 0.0f);
	assert (fixture_cvar ("vr_haptic")->value == 1.0f);
	assert (fixture_cvar ("vr_joystick_axis_deadzone")->value == 0.25f);
	assert (fixture_cvar ("vr_joystick_axis_menu_deadzone_extra")->value == 0.25f);
	assert (fixture_cvar ("vr_joystick_axis_exponent")->value == 1.0f);
	assert (fixture_cvar ("vr_joystick_deadzone_trunc")->value == 1.0f);
	assert (fixture_cvar ("vr_movement_mode")->value == 0.0f);
	assert (fixture_cvar ("vr_movement_speed")->value == 1.0f);
	assert (fixture_cvar ("vr_snap_turn")->value == 0.0f);
	assert (fixture_cvar ("vr_180_snap_turn")->value == 1.0f);
	assert (fixture_cvar ("vr_turn_speed")->value == 2.0f);
	assert (fixture_cvar ("vr_joystick_yaw_multi")->value == 1.0f);
	assert (turn180_command != NULL);
	reset_events ();
	VR_InputCommands (NULL);
	assert (event_count == 0);
}

static void test_lifecycle_and_hysteresis (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_game;
	set_cvar ("vr_lefthanded", 0.0f);
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
	set_cvar ("vr_lefthanded", 0.0f);
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
	set_cvar ("vr_lefthanded", 1.0f);
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
	set_cvar ("vr_lefthanded", 0.0f);
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
	fixture_frame = &frame;
	set_cvar ("vr_haptic", 1.0f);
	set_cvar ("vr_lefthanded", 0.0f);
	native_clear_then_neutral (&frame);

	frame.hands[0].pressed = VRXR_BUTTON_MENU;
	frame.hands[1].trigger = 0.8f;
	escape_changes_context = 1;
	escape_reenters_clear = 1;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ESCAPE, 1);
	assert (haptic_count == 1);
	expect_haptic (0, 0);
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
	fixture_frame = NULL;
}

static void test_axis_threshold_edges_and_console_escape (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_game;
	set_cvar ("vr_lefthanded", 0);
	set_cvar ("vr_joystick_axis_deadzone", 0);
	set_cvar ("vr_joystick_axis_menu_deadzone_extra", 0);
	set_cvar ("vr_joystick_axis_exponent", 0);
	native_clear_then_neutral (&frame);
	VR_InputCommands (&frame);
	assert (event_count == 0); /* pow(0,0) must not manufacture a direction. */
	set_cvar ("vr_joystick_axis_deadzone", .75f);
	set_cvar ("vr_joystick_axis_menu_deadzone_extra", .5f);
	set_cvar ("vr_joystick_axis_exponent", 1);
	frame.hands[1].stick[1] = 1;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* Configured combined threshold exceeds full scale. */
	set_cvar ("vr_joystick_axis_deadzone", .25f);
	set_cvar ("vr_joystick_axis_menu_deadzone_extra", .25f);
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
	fixture_frame = &frame;
	set_cvar ("vr_haptic", 1.0f);
	waiting_for_binding = false;
	input_grab_active = false;
	native_clear_then_neutral (&frame);

	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 1);
	assert (haptic_count == 1);
	expect_haptic (0, 1);
	reset_events ();
	VR_InputCommands (&frame); /* a held select is not another rising edge */
	assert (event_count == 0);
	assert (haptic_count == 0);
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
	assert (haptic_count == 0); /* capture input never fires menu feedback */
	fixture_frame = NULL;
}

static void test_menu_haptic_handedness_and_toggle (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_menu;
	waiting_for_binding = false;
	input_grab_active = false;
	fixture_frame = &frame;
	set_cvar ("vr_haptic", 1.0f);
	set_cvar ("vr_lefthanded", 1.0f);
	native_clear_then_neutral (&frame);

	/* With reversed roles, physical hand zero owns the logical-right trigger. */
	frame.hands[0].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 1);
	assert (haptic_count == 1);
	expect_haptic (0, 0);

	reset_events ();
	frame.hands[0].trigger = 0.0f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 0);
	assert (haptic_count == 0); /* release has no pulse */

	reset_events ();
	set_cvar ("vr_haptic", 0.0f);
	frame.hands[0].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 1);
	assert (haptic_count == 0); /* disabling feedback leaves key input intact */

	set_cvar ("vr_haptic", 1.0f);
	set_cvar ("vr_lefthanded", 0.0f);
	fixture_frame = NULL;
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
	fixture_frame = &frame;
	set_cvar ("vr_haptic", 1.0f);
	waiting_for_binding = false;
	input_grab_active = true;
	native_clear_then_neutral (&frame);

	frame.hands[1].pressed = VRXR_BUTTON_SECONDARY;
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_BBUTTON, 1);
	assert (haptic_count == 0);

	key_dest = key_game;
	frame = neutral_frame ();
	input_grab_active = true;
	native_clear_then_neutral (&frame);
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ABUTTON, 1);
	assert (haptic_count == 0); /* modal-grab decisions suppress haptics */

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
	assert (haptic_count == 0);
	fixture_frame = NULL;
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

static void motion_sample (vrxr_frame_t *frame)
{
	fixture_frame = frame;
	VR_InputCommands (frame);
	VR_InputMove (&cl.pendingcmd);
}

static void near_motion (float actual, float expected)
{
	assert (fabsf (actual - expected) < .002f);
}

static void test_motion_ownership_and_tracking_loss (void)
{
	vrxr_frame_t frame = neutral_frame ();
	usercmd_t cmd = {0};
	key_dest = key_game;
	input_grab_active = waiting_for_binding = angle_locked = false;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cl.intermission = cl.paused = cls.demoplayback = false;
	vr_aimmode.value = 7;
	cl_forwardspeed.value = cl_upspeed.value = 200;
	cl_movespeedkey.value = 2;
	cl_desktop_vanilla_run.value = cl_alwaysrun.value = 0;
	host_frametime = .01;
	fixture_turn_yaw = 0;
	fixture_turn_calls = 0;
	for (int i = 0; i < 3; ++i)
		frame.devices[i].valid = true;
	fixture_head_angles[YAW] = 30;
	fixture_hand_angles[0][YAW] = 90;
	fixture_hand_angles[1][YAW] = 150;
	set_cvar ("vr_lefthanded", 0);
	set_cvar ("vr_movement_mode", 0);
	set_cvar ("vr_movement_speed", 1);
	set_cvar ("vr_joystick_axis_deadzone", .25f);
	set_cvar ("vr_joystick_axis_exponent", 1);
	set_cvar ("vr_joystick_deadzone_trunc", 1);
	VR_InputClear ();
	reset_events ();
	for (int mode = 0; mode < 3; ++mode)
	{
		set_cvar ("vr_movement_mode", mode);
		frame.hands[0].stick[1] = 0;
		motion_sample (&frame);
		motion_sample (&frame);
		frame.hands[0].stick[1] = 1;
		motion_sample (&frame);
		assert (cl.pendingcmd.vr_pending_move_valid && cl.pendingcmd.vr_pending_angles_valid);
		near_motion (cl.pendingcmd.vr_pending_move[0], 200);
		near_motion (cl.pendingcmd.vr_pending_angles[YAW], 30 + 60 * mode);
		cmd = (usercmd_t){0};
		cmd.forwardmove = 17;
		VR_InputApplyPending (&cmd);
		near_motion (cmd.forwardmove, 217);
		usercmd_t before = cl.pendingcmd;
		int turns_before = fixture_turn_calls;
		for (int i = 0; i < 8; ++i)
		{
			usercmd_t preview = {0};
			VR_InputApplyPending (&preview);
			near_motion (preview.forwardmove, 200);
		}
		assert (!memcmp (&before, &cl.pendingcmd, sizeof before));
		assert (turns_before == fixture_turn_calls);
	}
	// RAW's command basis depends on the dominant pose. Neutral while that
	// pose is unavailable must not rearm a held stick on pose restoration.
	frame.devices[2].valid = false;
	frame.hands[0].stick[1] = 0;
	motion_sample (&frame);
	motion_sample (&frame);
	assert (!cl.pendingcmd.vr_pending_move_valid);
	frame.devices[2].valid = true;
	frame.hands[0].stick[1] = 1;
	motion_sample (&frame);
	assert (!cl.pendingcmd.vr_pending_move_valid);
	frame.hands[0].stick[1] = 0;
	motion_sample (&frame);
	frame.hands[0].stick[1] = 1;
	motion_sample (&frame);
	assert (cl.pendingcmd.vr_pending_move_valid);
	// A later authority lock suppresses the complete VR contribution while
	// retaining native input, without mutating the pending record on preview.
	angle_locked = true;
	cmd = (usercmd_t){0};
	cmd.forwardmove = 17;
	cmd.viewangles[YAW] = 11;
	VR_InputApplyPending (&cmd);
	near_motion (cmd.forwardmove, 17);
	near_motion (cmd.viewangles[YAW], 11);
	motion_sample (&frame);
	angle_locked = false;
	motion_sample (&frame);
	assert (!cl.pendingcmd.vr_pending_move_valid);
	frame.hands[0].stick[1] = 0;
	motion_sample (&frame);
	frame.hands[0].stick[1] = 1;
	motion_sample (&frame);
	cmd = (usercmd_t){0};
	cmd.forwardmove = 32760;
	VR_InputApplyPending (&cmd);
	near_motion (cmd.forwardmove, 32760);
	set_cvar ("vr_movement_mode", 1e30f); // bounds checked before integer conversion
	frame.hands[0].stick[1] = 0;
	motion_sample (&frame);
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_pending_angles[YAW], 30);
	set_cvar ("vr_snap_turn", 45);
	motion_sample (&frame);
	frame.hands[1].stick[0] = 1;
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, -45);
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, -45);
	frame.hands[1].stick[0] = -1;
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, 0);
	frame.hands[1].stick[0] = 0;
	turn180_command ();
	near_motion (fixture_turn_yaw, 0);
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, -180);
	set_cvar ("vr_snap_turn", 0);
	motion_sample (&frame);
	frame.hands[1].stick[0] = 1;
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, -182);
	frame.focused = false;
	motion_sample (&frame);
	frame.focused = true;
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, -182);
	cls.state = ca_disconnected;
	turn180_command ();
	cls.state = ca_connected;
	frame.hands[1].stick[0] = 0;
	motion_sample (&frame);
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, -182);
	// A profile-only change gates that hand's dependencies, not unrelated
	// locomotion/turn channels that remain held and valid.
	frame.hands[0].stick[1] = 1;
	motion_sample (&frame);
	assert (cl.pendingcmd.vr_pending_move_valid);
	frame.hands[1].profile = VRXR_PROFILE_INDEX;
	motion_sample (&frame);
	assert (cl.pendingcmd.vr_pending_move_valid);
	near_motion (cl.pendingcmd.vr_pending_move[0], 200);
	frame.hands[1].stick[0] = 0;
	motion_sample (&frame);
	frame.hands[1].stick[0] = 1;
	motion_sample (&frame);
	float prior_turn = fixture_turn_yaw;
	frame.hands[0].profile = VRXR_PROFILE_INDEX;
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, prior_turn - 2);
	fixture_frame = NULL;
}

static void test_roomscale_command_accumulator (void)
{
	vrxr_frame_t frame = neutral_frame ();
	usercmd_t preview = {0};
	key_dest = key_game;
	input_grab_active = waiting_for_binding = angle_locked = false;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cl.intermission = cl.paused = cls.demoplayback = false;
	vr_aimmode.value = 7;
	fixture_turn_yaw = 0;
	frame.devices[0].valid = true;
	VR_InputClear ();
	motion_sample (&frame); /* establish the raw HMD baseline */
	frame.devices[0].matrix[2][3] = -0.1f; /* forward in OpenXR */
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 1.0f);
	motion_sample (&frame); /* reused completed frame cannot double count */
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 1.0f);
	fixture_turn_yaw = 90.0f;
	frame.devices[0].matrix[0][3] = 0.2f; /* physical right, rotated into game forward */
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 3.0f);
	near_motion (cl.pendingcmd.vr_roomscalemove[1], 0.0f);
	for (int i = 0; i < 3; ++i)
	{
		preview = (usercmd_t){0};
		VR_InputApplyPending (&preview);
		near_motion (preview.vr_roomscalemove[0], 3.0f);
		near_motion (cl.pendingcmd.vr_roomscalemove[0], 3.0f);
		assert (!preview.vr_active);
	}
	frame.focused = false;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 0.0f);
	frame.focused = true;
	motion_sample (&frame); /* recenter/focus recovery takes a new baseline */
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 0.0f);
	frame.devices[0].matrix[2][3] -= 2.0f; /* outlier is dropped whole */
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 0.0f);
	frame.devices[0].matrix[2][3] -= 0.1f;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[1], 1.0f);
	angle_locked = true;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[1], 0.0f);
	angle_locked = false;
	VR_InputClear ();
	fixture_turn_yaw = 0;
	frame.devices[0].matrix[0][3] = 0;
	frame.devices[0].matrix[2][3] = 0;
	motion_sample (&frame);
	frame.devices[0].matrix[2][3] = -1.0f;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 10.0f);
	frame.devices[0].matrix[2][3] = -2.0f;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 20.0f);
	preview = (usercmd_t){0};
	VR_InputApplyPending (&preview);
	near_motion (preview.vr_roomscalemove[0], 0.0f); /* whole command exceeds PMove limit */
	frame.devices[0].matrix[2][3] = -1.0f;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 10.0f);
	frame.devices[0].matrix[2][3] = 0;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 0.0f); /* no invented return displacement */
	fixture_frame = NULL;
}

static void expect_no_private_pose (void)
{
	usercmd_t applied = {0};

	assert (!cl.pendingcmd.vr_active);
	assert (!cl.pendingcmd.vr_handpos_relative);
	VR_InputApplyPending (&applied);
	assert (!applied.vr_active);
	assert (!applied.vr_handpos_relative);
}

static void test_private_pose_for_handedness (qboolean lefthanded)
{
	vrxr_frame_t frame = neutral_frame ();
	usercmd_t applied = {0};
	usercmd_t pending_snapshot;
	vec3_t expected_angles, world_muzzle, expected_handpos;
	const int dominant = lefthanded ? 0 : 1;
	int body_calls, muzzle_calls;

	key_dest = key_game;
	input_grab_active = waiting_for_binding = angle_locked = false;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cl.intermission = cl.paused = cls.demoplayback = false;
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	vr_aimmode.value = 7;
	vr_gunmodelpitch.value = 19.0f;
	vr_gunmodelscale.value = 1.35f;
	fixture_units_per_metre = 10.0f;
	fixture_turn_yaw = 0.0f;
	fixture_turn_calls = 0;
	fixture_head_angles[PITCH] = -8.0f;
	fixture_head_angles[YAW] = 27.0f;
	fixture_head_angles[ROLL] = 3.0f;
	fixture_hand_angles[dominant][PITCH] = 16.0f;
	fixture_hand_angles[dominant][YAW] = 43.0f;
	fixture_hand_angles[dominant][ROLL] = -21.0f;
	fixture_body_offset_available = true;
	fixture_muzzle_available = true;
	fixture_require_pose_identity = true;
	fixture_last_body_hand = -1;
	fixture_body_offset_calls = 0;
	fixture_muzzle_calls = 0;
	set_cvar ("vr_lefthanded", lefthanded ? 1.0f : 0.0f);
	set_cvar ("vr_movement_mode", VR_MOVEMENT_MODE_FOLLOW_HEAD);
	set_cvar ("vr_movement_speed", 1.0f);
	set_cvar ("vr_joystick_axis_deadzone", 0.25f);
	set_cvar ("vr_joystick_axis_exponent", 1.0f);
	set_cvar ("vr_joystick_deadzone_trunc", 1.0f);

	frame.should_render = 1;
	for (int device = 0; device < 3; ++device)
		frame.devices[device].valid = true;
	frame.devices[0].kind = VRXR_DEVICE_HEAD;
	for (int hand = 0; hand < 2; ++hand)
	{
		frame.devices[hand + 1].kind = VRXR_DEVICE_HAND;
		frame.devices[hand + 1].hand = hand;
	}
	assert (frame.focused && frame.should_render && frame.devices[0].valid);
	assert (frame.devices[dominant + 1].valid &&
		frame.devices[dominant + 1].kind == VRXR_DEVICE_HAND &&
		frame.devices[dominant + 1].hand == dominant);
	fixture_frame = &frame;

	VR_InputClear ();
	reset_events ();
	VR_InputCommands (&frame); /* establish the same active profile/role identity */
	VR_InputCommands (&frame); /* neutral sample rearms the stable identity */
	assert (event_count == 0);
	motion_sample (&frame); /* establish the cumulative roomscale baseline */
	assert (cl.pendingcmd.vr_roomscalemove[0] == 0.0f);
	frame.devices[0].matrix[0][3] = 0.05f;
	frame.devices[0].matrix[2][3] = -0.10f;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 1.0f);
	near_motion (cl.pendingcmd.vr_roomscalemove[1], -0.5f);
	frame.devices[0].matrix[0][3] = 0.10f;
	frame.devices[0].matrix[2][3] = -0.20f;
	motion_sample (&frame);
	near_motion (cl.pendingcmd.vr_roomscalemove[0], 2.0f);
	near_motion (cl.pendingcmd.vr_roomscalemove[1], -1.0f);
	assert (fixture_last_body_hand == dominant);
	assert (fixture_body_offset_calls > 0 && fixture_muzzle_calls > 0);
	assert (cl.pendingcmd.vr_active && cl.pendingcmd.vr_handpos_relative);

	VectorCopy (fixture_hand_angles[dominant], expected_angles);
	expected_angles[YAW] += fixture_turn_yaw;
	assert (VR_LocomotionMuzzleOffsetToWorld (fixture_local_muzzle,
		expected_angles, vr_gunmodelscale.value, vr_gunmodelpitch.value,
		lefthanded, world_muzzle));
	for (int axis = 0; axis < 3; ++axis)
		expected_handpos[axis] = fixture_raw_grip[axis] + world_muzzle[axis] -
			cl.pendingcmd.vr_roomscalemove[axis];
	for (int axis = 0; axis < 3; ++axis)
	{
		near_motion (cl.pendingcmd.vr_handpos[axis], expected_handpos[axis]);
		near_motion (cl.pendingcmd.vr_handrot[axis], expected_angles[axis]);
	}

	applied.forwardmove = 13.0f;
	VR_InputApplyPending (&applied);
	assert (applied.vr_active && applied.vr_handpos_relative);
	cl.stats[STAT_HEALTH] = 100;
	assert (!VR_InputSuppressUncalibratedAttack (&applied));
	for (int axis = 0; axis < 3; ++axis)
	{
		near_motion (applied.vr_handpos[axis], expected_handpos[axis]);
		near_motion (applied.vr_handrot[axis], expected_angles[axis]);
		near_motion (applied.vr_roomscalemove[axis], cl.pendingcmd.vr_roomscalemove[axis]);
	}
	pending_snapshot = cl.pendingcmd;
	for (int preview_index = 0; preview_index < 5; ++preview_index)
	{
		usercmd_t preview = {0};
		VR_InputApplyPending (&preview);
		assert (preview.vr_active && preview.vr_handpos_relative);
		for (int axis = 0; axis < 3; ++axis)
		{
			near_motion (preview.vr_handpos[axis], expected_handpos[axis]);
			near_motion (preview.vr_handrot[axis], expected_angles[axis]);
		}
		assert (!memcmp (&pending_snapshot, &cl.pendingcmd, sizeof pending_snapshot));
	}

	/* Missing body grip pose, calibration muzzle, or matching tracked hand must
	 * leave the cleared private fields inactive through both production calls. */
	fixture_body_offset_available = false;
	motion_sample (&frame);
	expect_no_private_pose ();
	assert (VR_InputSuppressUncalibratedAttack (&(usercmd_t){0}));
	cl.stats[STAT_HEALTH] = 0;
	assert (!VR_InputSuppressUncalibratedAttack (&(usercmd_t){0}));
	cl.stats[STAT_HEALTH] = 100;
	fixture_body_offset_available = true;
	fixture_muzzle_available = false;
	motion_sample (&frame);
	expect_no_private_pose ();
	fixture_muzzle_available = true;
	fixture_local_muzzle[0] = NAN;
	motion_sample (&frame);
	expect_no_private_pose ();
	fixture_local_muzzle[0] = 2.0f;
	fixture_hand_angles[dominant][ROLL] = NAN;
	motion_sample (&frame);
	expect_no_private_pose ();
	fixture_hand_angles[dominant][ROLL] = -21.0f;
	frame.devices[dominant + 1].valid = false;
	motion_sample (&frame);
	expect_no_private_pose ();
	frame.devices[dominant + 1].valid = true;

	/* A public connection may use the same focused inputs, but has no private
	 * server pose admission and must not receive the prepared hand record. */
	cl.protocol_qsvr = 0;
	body_calls = fixture_body_offset_calls;
	muzzle_calls = fixture_muzzle_calls;
	motion_sample (&frame);
	expect_no_private_pose ();
	assert (!VR_InputSuppressUncalibratedAttack (&(usercmd_t){0}));
	assert (fixture_body_offset_calls == body_calls);
	assert (fixture_muzzle_calls == muzzle_calls);

	cl.protocol_qsvr = 0;
	fixture_require_pose_identity = false;
	fixture_frame = NULL;
}

static void test_private_pose_preparation (void)
{
	test_private_pose_for_handedness (false);
	test_private_pose_for_handedness (true);
	puts ("VR private controller pose: pinned admission, grip+muzzle-roomscale composition, previews, invalid inputs and handedness passed");
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
	test_menu_haptic_handedness_and_toggle ();
	test_capture_change_stops_same_batch_presses ();
	test_modal_grab_overrides_destination_and_rearms ();
	test_menu_state_change_stops_same_batch_presses ();
	test_modal_grab_only_emits_decision_keys ();
	test_modal_cancel_wins_over_confirm ();
	test_input_hands_are_snapshotted_before_release_callbacks ();
	test_motion_ownership_and_tracking_loss ();
	test_roomscale_command_accumulator ();
	test_private_pose_preparation ();
	puts ("VR input adapter preserves native key ownership, menu dispatch, gating and re-entry safety");
	puts ("VR movement: modes, preview immutability, tracking/authority rearm, wire bounds and snap/smooth/queued turning passed");
	puts ("VR roomscale preparation: mapped horizontal delta, deduplication, preview, focus and outlier gates passed");
	return 0;
}
