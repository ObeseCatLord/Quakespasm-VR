/* Exercise the production XR input adapter with native Key_Event recorded. */
#include "../Quake/quakedef.h"
#include "../Quake/menu.h"
#include "../Quake/vr_input.h"
#include "../Quake/vr_locomotion.h"
#include "../Quake/vr_fbt.h"
#include "../Quake/vr_fbt_filter.h"
#include "../Quake/vr_fbt_profile.h"
#include "../Quake/vr_fbt_storage.h"
#include "../Quake/vr_weapon_calibration.h"
#include "../Quake/view.h"
#include "../Quake/gl_model.h"
#include "../Quake/world.h"
#include "../Quake/r_vrik.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

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
cvar_t vr_gunmodely = {"vr_gunmodely", "0", CVAR_ARCHIVE};
cvar_t vr_gunmodelscale = {"vr_gunmodelscale", "1.35", CVAR_ARCHIVE};

static qboolean waiting_for_binding;
static qboolean input_grab_active;
static qboolean pointer_can_click;
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
static int registered_command_count;
static cmd_function_t registered_commands[32];
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
	assert (source == src_command && !qcinterceptable);
	assert (function != NULL);
	assert (registered_command_count < (int)(sizeof (registered_commands) / sizeof (registered_commands[0])));
	cmd_function_t *command = &registered_commands[registered_command_count++];
	memset (command, 0, sizeof (*command));
	command->name = name;
	command->function = function;
	command->srctype = source;
	if (!strcmp (name, "vr_turn180"))
		turn180_command = function;
	return command;
}

static cmd_function_t *fixture_command (const char *name)
{
	for (int i = 0; i < registered_command_count; ++i)
		if (!strcmp (registered_commands[i].name, name))
			return &registered_commands[i];
	return NULL;
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

qboolean M_VRPointerCanClick (void)
{
	return pointer_can_click;
}

static void fixture_clear_vec (vec3_t out)
{
	if (out)
		memset (out, 0, sizeof (vec3_t));
}

/* The isolated adapter fixture has no console, config store, or native menu. */
char *keybindings[MAX_KEYS];

void VRXR_SetTrackerEnabled (int enabled) { (void)enabled; }
qboolean VR_WeaponMenu_IsOpenVR (void) { return false; }

/* These external presentation/model owners are unavailable in this adapter
 * fixture. Fail closed rather than claiming native grip/MD5/menu integration. */
void VR_WeaponMenu_Cancel (void) {}
qboolean V_TrackedRawGripBodyOffset (int physical_hand, vec3_t out)
{
	(void)physical_hand;
	memset (out, 0, sizeof (vec3_t));
	return false;
}
entity_t *V_HeldMeleeEntity (void) { return NULL; }
qboolean Mod_GetMD5StockAxeEdge (const qmodel_t *mod, const aliashdr_t *selected,
	int pose0, int pose1, stockaxe_edge_t *out)
{
	(void)mod;
	(void)selected;
	(void)pose0;
	(void)pose1;
	memset (out, 0, sizeof (*out));
	return false;
}
qboolean M_VRPointerRequiresHit (void) { return false; }
qboolean VR_WeaponCalibrationAdjustActive (void) { return false; }
void VR_WeaponCalibrationAdjustCancel (void) {}
void VR_WeaponCalibrationAdjustInput (int hand, qboolean trigger, qboolean pose,
	const vec3_t origin, const vec3_t angles)
{ (void)hand; (void)trigger; (void)pose; (void)origin; (void)angles; }
qboolean VR_WeaponCalibrationLookupHeld (const char *name, qboolean enhanced,
	vec3_t offset, float *scale)
{ (void)name; (void)enhanced; fixture_clear_vec (offset); if (scale) *scale = 0; return false; }
qboolean VR_WeaponCalibrationLookupMelee (const char *name, vr_melee_gesture_profile_t *out)
{ (void)name; if (out) memset (out, 0, sizeof (*out)); return false; }
qboolean VR_WeaponCalibrationStockRangedViewmodel (const char *name)
{ (void)name; return false; }

/* Optional FBT is disabled here; these fail-closed seams do not test its policy. */
void VR_FBT_Init (vr_fbt_manager_t *manager) { if (manager) memset (manager, 0, sizeof (*manager)); }
int VR_FBT_SerialIsSafe (const char *serial) { (void)serial; return false; }
int VR_FBT_Reconcile (vr_fbt_manager_t *manager, uint64_t id, double time,
	const vr_fbt_candidate_t *candidates, unsigned int count)
{ (void)manager; (void)id; (void)time; (void)candidates; (void)count; return false; }
unsigned int VR_FBT_GetCandidateCount (const vr_fbt_manager_t *manager)
{ (void)manager; return 0; }
int VR_FBT_GetCandidate (const vr_fbt_manager_t *manager, unsigned int index,
	vr_fbt_candidate_status_t *status)
{ (void)manager; (void)index; if (status) memset (status, 0, sizeof (*status)); return false; }
int VR_FBT_GetRoleStatus (const vr_fbt_manager_t *manager, vr_fbt_role_t role,
	vr_fbt_role_status_t *status)
{ (void)manager; (void)role; if (status) memset (status, 0, sizeof (*status)); return false; }
int VR_FBT_AssignCandidate (vr_fbt_manager_t *manager, vr_fbt_role_t role, unsigned int index)
{ (void)manager; (void)role; (void)index; return false; }
int VR_FBT_BindSerial (vr_fbt_manager_t *manager, vr_fbt_role_t role, const char *serial)
{ (void)manager; (void)role; (void)serial; return false; }
int VR_FBT_UnassignRole (vr_fbt_manager_t *manager, vr_fbt_role_t role)
{ (void)manager; (void)role; return false; }
void VR_FBT_FilterInit (vr_fbt_filter_t *filter)
{ if (filter) memset (filter, 0, sizeof (*filter)); }
int VR_FBT_ProfileCaptureBegin (vr_fbt_profile_capture_t *capture,
	unsigned int roles, const char *const serials[VR_FBT_ROLE_COUNT])
{ (void)capture; (void)roles; (void)serials; return false; }
int VR_FBT_ProfileCaptureAddSnapshot (vr_fbt_profile_capture_t *capture,
	uint64_t id, double time, const vr_fbt_profile_capture_sample_t samples[VR_FBT_ROLE_COUNT])
{ (void)capture; (void)id; (void)time; (void)samples; return false; }
int VR_FBT_ProfileCaptureFinalize (const vr_fbt_profile_capture_t *capture,
	const vr_fbt_profile_capture_metadata_t *metadata, vr_fbt_profile_t *destination,
	vr_fbt_profile_error_t *error)
{ (void)capture; (void)metadata; (void)destination; (void)error; return false; }
int VR_FBT_StorageNameIsSafe (const char *name) { (void)name; return false; }
int VR_FBT_StorageLoadProfile (const char *name, vr_fbt_profile_t *profile,
	vr_fbt_profile_error_t *profile_error, vr_fbt_storage_error_t *storage_error)
{ (void)name; (void)profile; (void)profile_error; (void)storage_error; return false; }
int VR_FBT_StorageSaveProfile (const vr_fbt_profile_t *profile,
	vr_fbt_profile_error_t *profile_error, vr_fbt_storage_error_t *storage_error)
{ (void)profile; (void)profile_error; (void)storage_error; return false; }
int VR_FBT_StorageLoadSelected (char *name, size_t capacity, vr_fbt_storage_error_t *error)
{ (void)name; (void)capacity; (void)error; return false; }
int VR_FBT_StorageSaveSelected (const char *name, vr_fbt_storage_error_t *error)
{ (void)name; (void)error; return false; }

/* Presentation and optional paired-weapon owners are outside this fixture. */
qboolean V_TrackedPlayerBase (float *height) { if (height) *height = 0; return false; }
qboolean V_TrackedPresentationYaw (float *yaw) { if (yaw) *yaw = 0; return false; }
qboolean V_TrackedPresentationHandAngles (int hand, vec3_t angles)
{ (void)hand; fixture_clear_vec (angles); return false; }
qboolean V_TrackedPresentationHandWorldPose (int hand, vec3_t origin, vec3_t angles)
{ (void)hand; fixture_clear_vec (origin); fixture_clear_vec (angles); return false; }
qboolean V_AkimboPairReady (void) { return false; }
void V_AkimboPairCollisionOffset (int hand, vec3_t offset)
{ (void)hand; fixture_clear_vec (offset); }
qboolean V_AkimboRecipeSupported (const char *name) { (void)name; return false; }
qboolean V_AkimboRecipeUsesPairedCollision (const char *name) { (void)name; return false; }
qboolean V_AkimboModelAngles (const char *name, int hand, const vec3_t raw, vec3_t out)
{ (void)name; (void)hand; (void)raw; fixture_clear_vec (out); return false; }
qboolean V_AkimboTransformAnchor (int hand, const vec3_t angles, vec3_t out)
{ (void)hand; (void)angles; fixture_clear_vec (out); return false; }
qboolean V_AkimboDwellEdgeOffsets (int hand, const vec3_t angles, vec3_t base, vec3_t tip)
{ (void)hand; (void)angles; fixture_clear_vec (base); fixture_clear_vec (tip); return false; }
qboolean V_HeldMeleeEdgeOffsets (vec3_t base, vec3_t tip, vec3_t collision)
{ fixture_clear_vec (base); fixture_clear_vec (tip); fixture_clear_vec (collision); return false; }
qboolean V_HeldMeleeRawEdgeOffsets (const vec3_t angles, vec3_t base, vec3_t tip)
{ (void)angles; fixture_clear_vec (base); fixture_clear_vec (tip); return false; }
qboolean CL_ResolveWeaponCollision (const vec3_t torso, const vec3_t grip,
	const vec3_t base, const vec3_t tip, vec3_t delta)
{ (void)torso; (void)grip; (void)base; (void)tip; fixture_clear_vec (delta); return false; }

int R_AliasViewmodelHandMatrix (entity_t *entity, const aliashdr_t *geometry,
	lerpdata_t *lerpdata, float matrix[16], int hand)
{ (void)entity; (void)geometry; (void)lerpdata; (void)matrix; (void)hand; return false; }
qboolean R_TrackedHeadEyeHeight (float base, float *height)
{ (void)base; if (height) *height = 0; return false; }
qboolean R_TrackedHeadBodyOffset (vec3_t offset)
{ fixture_clear_vec (offset); return false; }
qboolean R_VRIKProjectCalibrationReference (qmodel_t *model,
	const r_vrik_calibration_projection_input_t *input, r_vrik_calibration_projection_t *out)
{ (void)model; (void)input; if (out) memset (out, 0, sizeof (*out)); return false; }
void *Mod_Extradata_CheckSkin (qmodel_t *model, int skin)
{ (void)model; (void)skin; return NULL; }
const mod_akimbo_pair_recipe_t *Mod_GetAkimboPairRecipe (const char *name)
{ (void)name; return NULL; }
const mod_held_melee_recipe_t *Mod_GetHeldMeleeRecipe (const char *name)
{ (void)name; return NULL; }
qboolean Mod_GetStockAxeEdge (qmodel_t *model, int skin, stockaxe_edge_t *out)
{ (void)model; (void)skin; if (out) memset (out, 0, sizeof (*out)); return false; }
qboolean Mod_GetAlkalineAxeEdge (qmodel_t *model, int skin, stockaxe_edge_t *out)
{ (void)model; (void)skin; if (out) memset (out, 0, sizeof (*out)); return false; }
qboolean Mod_GetCopperAxeEdge (qmodel_t *model, int skin, stockaxe_edge_t *out)
{ (void)model; (void)skin; if (out) memset (out, 0, sizeof (*out)); return false; }

/* Unused console/filesystem entry points fail closed for registered commands. */
void Con_Printf (const char *format, ...) { (void)format; }
void Con_Warning (const char *format, ...) { (void)format; }
int Cmd_Argc (void) { return 0; }
const char *Cmd_Argv (int arg) { (void)arg; return ""; }
qboolean Cmd_AliasExists (const char *name) { (void)name; return false; }
double Cvar_VariableValue (const char *name) { (void)name; return 0; }
static int fixture_cycle_impulses;
void Cbuf_AddText (const char *text)
{
	if (!strcmp (text, "impulse 10\n") || !strcmp (text, "impulse 12\n"))
		++fixture_cycle_impulses;
}
void Key_SetBinding (int key, const char *binding) { (void)key; (void)binding; }
byte *COM_LoadFile (const char *path, unsigned int *path_id) { (void)path; (void)path_id; return NULL; }
const char *COM_GetWriteRoot (void) { return NULL; }
void Mem_Free (const void *pointer) { (void)pointer; }
findfile_t *Sys_FindFirst (const char *directory, const char *extension)
{ (void)directory; (void)extension; return NULL; }
findfile_t *Sys_FindNext (findfile_t *find) { (void)find; return NULL; }
void Sys_FindClose (findfile_t *find) { (void)find; }
double Sys_DoubleTime (void) { return 0; }
int q_strcasecmp (const char *left, const char *right) { return strcasecmp (left, right); }
char *q_strcasestr (const char *haystack, const char *needle)
{ return haystack && needle ? strcasestr (haystack, needle) : NULL; }
int q_snprintf (char *buffer, size_t size, const char *format, ...)
{ va_list args; va_start (args, format); int result = vsnprintf (buffer, size, format, args); va_end (args); return result; }
size_t q_strlcpy (char *destination, const char *source, size_t size)
{
	size_t length = strlen (source);
	if (size)
	{
		size_t copied = length < size - 1 ? length : size - 1;
		memcpy (destination, source, copied);
		destination[copied] = '\0';
	}
	return length;
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
	static const char *expected_commands[] = {
		"vr_turn180", "vr_defaultbindings",
		"vr_fbt_list", "vr_fbt_assign", "vr_fbt_unassign",
		"vr_fbt_profile_select", "vr_fbt_profile_reset", "vr_fbt_profile_list",
		"vr_fbt_profile_save", "vr_fbt_calibrate_begin", "vr_fbt_calibrate_capture",
		"vr_fbt_calibrate_accept", "vr_fbt_calibrate_cancel"
	};
	registered_cvars = 0;
	memset (registered_cvar, 0, sizeof (registered_cvar));
	registered_command_count = 0;
	memset (registered_commands, 0, sizeof (registered_commands));
	turn180_command = NULL;
	VR_InputInit ();
	assert (registered_cvars == 17);
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
	assert (fixture_cvar ("vr_gorilla") != NULL);
	assert (fixture_cvar ("vr_vrik") != NULL);
	assert (fixture_cvar ("vr_immersive_melee") != NULL);
	assert (fixture_cvar ("vr_fbt_enabled") != NULL);
	assert (fixture_cvar ("vr_weapon_collision") != NULL);
	assert (registered_command_count == (int)(sizeof (expected_commands) / sizeof (expected_commands[0])));
	for (int i = 0; i < (int)(sizeof (expected_commands) / sizeof (expected_commands[0])); ++i)
		assert (fixture_command (expected_commands[i]) != NULL);
	assert (turn180_command != NULL);
	assert (fixture_command ("vr_turn180")->function == turn180_command);
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

static void test_index_wheel_touch (void)
{
	vrxr_frame_t frame = neutral_frame ();
	char *saved_binding = keybindings[K_RTHUMB];
	keybindings[K_RTHUMB] = "+vr_weaponmenu";
	key_dest = key_game;
	set_cvar ("vr_lefthanded", 0.0f);
	frame.hands[0].profile = frame.hands[1].profile = VRXR_PROFILE_INDEX;
	native_clear_then_neutral (&frame);
	frame.hands[0].touched = VRXR_BUTTON_PAD;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* Only the dominant touch opens the wheel. */
	frame.hands[1].touched = VRXR_BUTTON_PAD;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_RTHUMB, 1);
	reset_events ();
	frame.hands[1].touched = 0;
	VR_InputCommands (&frame);
	expect_event (0, K_RTHUMB, 0);

	/* Focus recovery while a finger rests on the pad cannot reopen the wheel.
	 * Discrete buttons independently rearm without waiting for that finger. */
	frame.hands[1].touched = VRXR_BUTTON_PAD;
	frame.hands[1].pad[0] = .8f;
	frame.focused = 0;
	VR_InputCommands (&frame);
	reset_events ();
	frame.focused = 1;
	VR_InputCommands (&frame);
	VR_InputCommands (&frame);
	assert (event_count == 0);
	frame.hands[1].pressed = VRXR_BUTTON_PRIMARY;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_XBUTTON, 1);
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	reset_events ();
	frame.hands[1].touched = 0;
	VR_InputCommands (&frame);
	frame.hands[1].touched = VRXR_BUTTON_PAD;
	VR_InputCommands (&frame);
	expect_event (0, K_RTHUMB, 1);

	key_dest = key_menu;
	waiting_for_binding = false;
	frame.hands[0].touched = frame.hands[1].touched = 0;
	native_clear_then_neutral (&frame);
	frame.hands[1].touched = VRXR_BUTTON_PAD;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* Ordinary menus ignore resting pad touches. */
	waiting_for_binding = true;
	frame.hands[1].touched = 0;
	native_clear_then_neutral (&frame);
	frame.hands[1].touched = VRXR_BUTTON_PAD;
	VR_InputCommands (&frame);
	expect_event (0, K_RTHUMB, 1);
	waiting_for_binding = false;

	key_dest = key_game;
	set_cvar ("vr_lefthanded", 1.0f);
	frame.hands[0].touched = frame.hands[1].touched = 0;
	native_clear_then_neutral (&frame);
	frame.hands[0].touched = VRXR_BUTTON_PAD;
	VR_InputCommands (&frame);
	expect_event (0, K_RTHUMB, 1);
	set_cvar ("vr_lefthanded", 0.0f);
	keybindings[K_RTHUMB] = saved_binding;
	puts ("VR wheel: Index touch, release, focus rearm, binding capture and handedness passed");
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
	pointer_can_click = false;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 4);
	expect_event (0, K_RIGHTARROW, 1);
	expect_event (1, K_LTRIGGER, 1);
	expect_event (2, K_VR_RIGHT_STICK_DOWN, 1);
	expect_event (3, K_ENTER, 1);

	reset_events ();
	frame.hands[0].pad[0] = 0.40f;
	frame.hands[1].stick[1] = 0.0f;
	frame.hands[0].trigger = 0.0f;
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
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
	pointer_can_click = true;
	native_clear_then_neutral (&frame);

	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	assert (event_count == 0); /* menu choice waits for the panel's postdraw hit test */
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	expect_event (0, K_MOUSE1, 1);
	assert (haptic_count == 1);
	expect_haptic (0, 1);
	reset_events ();
	pointer_can_click = false; /* hover changes cannot retarget a held trigger */
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 0);
	assert (haptic_count == 0);
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_MOUSE1, 0);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	assert (haptic_count == 0);

	/* A drawn panel without a clickable target selects Enter. */
	native_clear_then_neutral (&frame);
	frame.hands[1].trigger = 0.60f;
	pointer_can_click = false;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 1);
	assert (haptic_count == 1);
	expect_haptic (0, 1);
	reset_events ();
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 0);

	/* If no menu panel was drawn, even a stale pointer hit falls back to Enter. */
	native_clear_then_neutral (&frame);
	frame.hands[1].trigger = 0.60f;
	pointer_can_click = true;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, false);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 1);
	reset_events ();
	pointer_can_click = false;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 0); /* later hover changes do not reactivate the press */
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
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1); /* binding capture keeps the physical trigger mapping */
	assert (haptic_count == 0);
	fixture_frame = NULL;
}

static void test_deferred_menu_trigger_context_and_focus_gates (void)
{
	vrxr_frame_t frame = neutral_frame ();
	key_dest = key_menu;
	waiting_for_binding = false;
	input_grab_active = false;
	pointer_can_click = true;
	fixture_frame = &frame;
	native_clear_then_neutral (&frame);

	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	key_dest = key_game; /* destination changed before the postdraw callback */
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 0);
	assert (haptic_count == 0);

	key_dest = key_menu;
	VR_InputCommands (&frame); /* held input stays gated after context loss */
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 0);
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	expect_event (0, K_MOUSE1, 1);
	assert (haptic_count == 1);

	reset_events ();
	frame.focused = false;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	expect_event (0, K_MOUSE1, 0);
	assert (haptic_count == 0);
	reset_events ();
	frame.focused = true;
	VR_InputCommands (&frame); /* focus restoration does not revive held input */
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 0);
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	expect_event (0, K_MOUSE1, 1);
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
	VR_InputMenuPanelTrigger (&frame, false);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 1);
	assert (haptic_count == 1);
	expect_haptic (0, 0);

	reset_events ();
	frame.hands[0].trigger = 0.0f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	expect_event (0, K_ENTER, 0);
	assert (haptic_count == 0); /* release has no pulse */

	reset_events ();
	set_cvar ("vr_haptic", 0.0f);
	frame.hands[0].trigger = 0.60f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, false);
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
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	expect_event (0, K_BBUTTON, 1);
	assert (haptic_count == 0);

	key_dest = key_game;
	frame = neutral_frame ();
	input_grab_active = true;
	native_clear_then_neutral (&frame);
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1);
	expect_event (0, K_ABUTTON, 1);
	assert (haptic_count == 0); /* modal-grab decisions suppress haptics */

	reset_events ();
	input_grab_active = false;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 1); /* modal release, no stale RTRIGGER press */
	expect_event (0, K_ABUTTON, 0);
	reset_events ();
	frame.hands[1].trigger = 0.0f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 0);
	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
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
	assert (event_count == 1);
	expect_event (0, K_ABUTTON, 1);
	VR_InputMenuPanelTrigger (&frame, false);
	assert (event_count == 4);
	expect_event (1, K_ENTER, 1);
	expect_event (2, K_ABUTTON, 0);
	expect_event (3, K_ENTER, 0);
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
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 0); /* right primary is unrelated during a modal grab */

	frame.hands[0].stick[0] = 0.9f;
	frame.hands[0].stick[1] = -0.9f;
	frame.hands[1].pressed |= VRXR_BUTTON_GRIP | VRXR_BUTTON_STICK | VRXR_BUTTON_PAD;
	frame.hands[1].stick[1] = 0.9f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
	assert (event_count == 0); /* unrelated buttons and axes are ignored */

	frame.hands[1].trigger = 0.60f;
	VR_InputCommands (&frame);
	VR_InputMenuPanelTrigger (&frame, true);
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
	VR_InputMenuPanelTrigger (&frame, true);
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

static void test_wheel_motion_and_vive_cycle (void)
{
	vrxr_frame_t frame = neutral_frame ();
	char *saved_binding = keybindings[K_RTHUMB];
	keybindings[K_RTHUMB] = "+vr_weaponmenu";
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
	set_cvar ("vr_lefthanded", 0);
	set_cvar ("vr_snap_turn", 45);
	set_cvar ("vr_movement_mode", 0);
	for (int i = 0; i < 3; ++i)
		frame.devices[i].valid = true;
	frame.hands[0].profile = frame.hands[1].profile = VRXR_PROFILE_INDEX;
	VR_InputClear ();
	motion_sample (&frame);
	motion_sample (&frame);
	frame.hands[1].touched = VRXR_BUTTON_PAD;
	frame.hands[1].pad[0] = .8f;
	fixture_turn_yaw = 0;
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, 0);
	assert (frame.hands[1].pad[0] == .8f); /* Raw wheel input remains intact. */
	frame.focused = false;
	motion_sample (&frame);
	frame.focused = true;
	motion_sample (&frame);
	motion_sample (&frame);
	frame.hands[0].stick[1] = 1;
	frame.hands[1].stick[0] = 1;
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, -45);
	assert (cl.pendingcmd.vr_pending_move_valid);
	near_motion (cl.pendingcmd.vr_pending_move[0], 200);
	assert (frame.hands[1].pad[0] == .8f);

	frame = neutral_frame ();
	frame.hands[1].profile = VRXR_PROFILE_VIVE;
	native_clear_then_neutral (&frame);
	fixture_cycle_impulses = 0;
	frame.hands[1].pressed = VRXR_BUTTON_PAD;
	frame.hands[1].pad[0] = .8f;
	VR_InputCommands (&frame);
	int thumb_event = -1;
	for (int i = 0; i < event_count; ++i)
		if (events[i].key == K_RTHUMB)
			thumb_event = i;
	assert (thumb_event >= 0);
	expect_event (thumb_event, K_RTHUMB, 1);
	assert (fixture_cycle_impulses == 0);
	keybindings[K_RTHUMB] = "+jump";
	frame.hands[1].pressed = 0;
	VR_InputCommands (&frame);
	frame.hands[1].pressed = VRXR_BUTTON_PAD;
	VR_InputCommands (&frame);
	assert (fixture_cycle_impulses == 1); /* Custom click retains legacy cycling. */
	keybindings[K_RTHUMB] = saved_binding;
	fixture_frame = NULL;
	puts ("VR wheel: off-center touch preserves stick motion and Vive wheel excludes cycling");
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
	frame.hands[0].stick[1] = 0; // reconnect requires neutral before locomotion rearms
	frame.hands[1].stick[0] = 0;
	motion_sample (&frame);
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, -182);
	// A profile change rebases tracked pose histories and requires neutral
	// before locomotion/turning can use the new controller convention.
	frame.hands[0].stick[1] = 1;
	motion_sample (&frame);
	assert (cl.pendingcmd.vr_pending_move_valid);
	frame.hands[1].profile = VRXR_PROFILE_INDEX;
	motion_sample (&frame);
	assert (!cl.pendingcmd.vr_pending_move_valid);
	frame.hands[0].stick[1] = 0;
	motion_sample (&frame);
	motion_sample (&frame);
	frame.hands[0].stick[1] = 1;
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
	near_motion (fixture_turn_yaw, prior_turn);
	frame.hands[0].stick[1] = frame.hands[1].stick[0] = 0;
	motion_sample (&frame);
	motion_sample (&frame);
	frame.hands[1].stick[0] = 1;
	motion_sample (&frame);
	near_motion (fixture_turn_yaw, prior_turn - 2);

	/* A tracking/focus interruption must not make left locomotion depend on
	 * releasing grip. Its centered stick can rearm while button edges remain
	 * gated, then deliver movement on the following deflection. */
	frame.hands[0].stick[1] = 1;
	frame.hands[1].stick[0] = 0;
	frame.hands[0].pressed = VRXR_BUTTON_GRIP;
	frame.focused = false;
	motion_sample (&frame);
	assert (!cl.pendingcmd.vr_pending_move_valid);
	reset_events ();
	frame.focused = true;
	motion_sample (&frame);
	assert (!cl.pendingcmd.vr_pending_move_valid); /* held deflection stays gated */
	frame.hands[0].stick[1] = 0;
	motion_sample (&frame);
	motion_sample (&frame);
	frame.hands[0].stick[1] = 1;
	motion_sample (&frame);
	assert (cl.pendingcmd.vr_pending_move_valid);
	near_motion (cl.pendingcmd.vr_pending_move[0], 200);
	assert (event_count == 0); /* held grip does not re-press native bindings */
	cmd = (usercmd_t){0};
	cmd.forwardmove = 17;
	VR_InputApplyPending (&cmd);
	near_motion (cmd.forwardmove, 217);
	frame.hands[0].pressed = 0;
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
	test_index_wheel_touch ();
	test_button_profiles_and_menu_axes ();
	test_context_reentry_clear_and_nan ();
	test_axis_threshold_edges_and_console_escape ();
	test_menu_trigger_dispatch_and_capture_transition ();
	test_deferred_menu_trigger_context_and_focus_gates ();
	test_menu_haptic_handedness_and_toggle ();
	test_capture_change_stops_same_batch_presses ();
	test_modal_grab_overrides_destination_and_rearms ();
	test_menu_state_change_stops_same_batch_presses ();
	test_modal_grab_only_emits_decision_keys ();
	test_modal_cancel_wins_over_confirm ();
	test_input_hands_are_snapshotted_before_release_callbacks ();
	test_wheel_motion_and_vive_cycle ();
	test_motion_ownership_and_tracking_loss ();
	test_roomscale_command_accumulator ();
	test_private_pose_preparation ();
	puts ("VR input adapter preserves native key ownership, menu dispatch, gating and re-entry safety");
	puts ("VR movement: modes, preview immutability, tracking/authority rearm, wire bounds and snap/smooth/queued turning passed");
	puts ("VR roomscale preparation: mapped horizontal delta, deduplication, preview, focus and outlier gates passed");
	return 0;
}
