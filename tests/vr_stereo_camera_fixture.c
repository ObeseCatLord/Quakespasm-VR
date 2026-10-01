/* Execute the production renderer camera adapter and host-abort cleanup.
 * GPU waiting is spied: this verifies the drain boundary, not a Vulkan fence. */
#include "../Quake/gl_rmain.c"
#include "../Quake/gl_screen.c"
#include "../Quake/view.c"
#include "../Quake/chase.c"

vulkanglobals_t		 vulkan_globals;
client_state_t cl;
client_static_t cls;
server_t sv;
double host_frametime;
qboolean noclip_anglehack;
qboolean con_forcedup;
int r_trace_line_cache_counter;
cvar_t lookspring;
cvar_t r_waterwarp;
static int chase_traces;
/* Camera-owner fixture: controller assembly is covered by the native input
 * probe. Observe invalidation without introducing a duplicate input policy. */
static int motion_invalidations;
void VR_InputInvalidateMotion (void) { ++motion_invalidations; }
void VR_InputApplyPending (usercmd_t *cmd) { (void)cmd; }
/* Optional calibration/UI owners are outside this camera fixture. */
qboolean VR_InputFBTCalibrationVisualSnapshot (const vrxr_frame_t *frame,
 vr_input_fbt_visual_snapshot_t *snapshot) { (void)frame; (void)snapshot; return false; }
void VR_WeaponCalibrationAdjustCancel (void) {}
int VR_InputDominantPhysicalHand (void) { return 1; }
qboolean VR_WeaponCalibrationAdjustPresentation (vec3_t origin, vec3_t angles)
{ (void)origin; (void)angles; return false; }
qboolean VR_WeaponMenu_IsOpenVR (void) { return false; }
qboolean R_UseAlphaSort (void) { return false; }
void CL_ResetPredictionSmoothing (void) {}
qboolean CL_EvaluatePredictionViewOffset (vec3_t offset, qboolean camera_eligible)
{ (void)camera_eligible; VectorCopy (vec3_origin, offset); return false; }
mleaf_t *Mod_PointInLeaf (float *point, qmodel_t *model)
{
	static mleaf_t dry_leaf;
	assert (point && model);
	dry_leaf.contents = CONTENTS_EMPTY;
	return &dry_leaf;
}
qboolean SV_RecursiveHullCheck (hull_t *hull, vec3_t p1, vec3_t p2, trace_t *trace, unsigned int hitcontents)
{
	// Run the real chase calculation with a known collision point, followed
	// by an unobstructed view ray. GPU smoke uses the real map hull instead.
	if (chase_traces++ % 2 == 0)
	{
		trace->endpos[0] = 10;
		trace->endpos[1] = 20;
		trace->endpos[2] = 30;
	}
	else
		VectorCopy (p2, trace->endpos);
	return true;
}
static vrxr_frame_t	 test_frame;
static unsigned char uniform_data[160];
static int			 gpu_pending, gpu_drains, xr_aborts;
const vrxr_frame_t	*GL_OpenXRFrame (void)
{
	return vulkan_globals.stereo_active ? &test_frame : NULL;
}
byte *R_UniformAllocate (int size, VkBuffer *buffer, uint32_t *offset, VkDescriptorSet *set)
{
	assert (size == sizeof uniform_data);
	*buffer = (VkBuffer)(uintptr_t)1;
	*offset = 0;
	*set = (VkDescriptorSet)(uintptr_t)1;
	return uniform_data;
}
void GL_WaitForDeviceIdle (void)
{
	++gpu_drains;
	gpu_pending = 0;
}
void VRXR_AbortFrame (void)
{
	assert (!gpu_pending);
	++xr_aborts;
}
static void near_value (float actual, float expected)
{
	assert (fabsf (actual - expected) < .002f);
}
static void pose_matrix (float matrix[3][4], float yaw, float pitch, float roll,
	float x, float y, float z)
{
	const float yr = yaw * (float)(M_PI / 180.0);
	const float pr = pitch * (float)(M_PI / 180.0);
	const float rr = roll * (float)(M_PI / 180.0);
	const float cy = cosf (yr), sy = sinf (yr);
	const float cp = cosf (pr), sp = sinf (pr);
	const float cr = cosf (rr), sr = sinf (rr);
	const float rotation[3][3] = {
		{cy * cr + sy * sp * sr, -cy * sr + sy * sp * cr, sy * cp},
		{cp * sr, cp * cr, -sp},
		{-sy * cr + cy * sp * sr, sy * sr + cy * sp * cr, cy * cp}
	};
	for (int row = 0; row < 3; ++row)
	{
		for (int column = 0; column < 3; ++column)
			matrix[row][column] = rotation[row][column];
		matrix[row][3] = row == 0 ? x : row == 1 ? y : z;
	}
}

static void set_rigid_pose (float x, float y, float z, float pitch,
	float yaw, float roll)
{
	float (*head)[4] = test_frame.devices[0].matrix;
	pose_matrix (head, yaw, pitch, roll, x, y, z);
	for (int eye = 0; eye < 2; ++eye)
	{
		memcpy (test_frame.views[eye].matrix, head, sizeof test_frame.views[eye].matrix);
		for (int row = 0; row < 3; ++row)
			test_frame.views[eye].matrix[row][3] += head[row][0] * (eye ? .032f : -.032f);
	}
}

static void head_yaw (float degrees)
{
	const float (*head)[4] = test_frame.devices[0].matrix;
	set_rigid_pose (head[0][3], head[1][3], head[2][3], 0, degrees, 0);
}
static void restore (void)
{
	R_RestoreStereoView ();
	near_value (r_refdef.vieworg[0], 100);
	near_value (r_refdef.vieworg[1], 200);
	near_value (r_refdef.vieworg[2], 300);
	for (int i = 0; i < 3; ++i)
		near_value (r_refdef.viewangles[i], 0);
}

static void test_hand_body_offset (void)
{
	const float units = V_VRUnitsPerMetre ();
	float base_viewheight, eye_height, prepared_eye_height;
	vec3_t grip = {1, 2, 3};
	vrxr_device_t *head = &test_frame.devices[0];
	vrxr_device_t *hand = &test_frame.devices[1];

	cls.signon = SIGNONS;
	cls.demoplayback = false;
	cl.intermission = 0;
	cl.paused = false;
	cl.fixangle_time = -1;
	cl.protocol_qsvr = 0;
	cl.cmd.vr_active = cl.pendingcmd.vr_active = false;
	cl.stats[STAT_VIEWHEIGHT] = 22;
	chase_active.value = 0;
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	vr_world_scale.value = 1;
	vr_floor_offset.value = -10;
	test_frame.should_render = test_frame.focused = test_frame.floor_referenced = 1;
	test_frame.reference_changed = 0;
	memset (head, 0, sizeof (*head));
	memset (hand, 0, sizeof (*hand));
	for (int i = 0; i < 3; ++i)
	{
		head->matrix[i][i] = hand->matrix[i][i] = 1;
	}
	head->valid = 1;
	head->kind = VRXR_DEVICE_HEAD;
	head->hand = -1;
	head->matrix[0][3] = 1;
	head->matrix[1][3] = 1.5f;
	head->matrix[2][3] = 2;
	hand->valid = 1;
	hand->kind = VRXR_DEVICE_HAND;
	hand->hand = 0;
	hand->matrix[0][3] = .75f;
	hand->matrix[1][3] = 1.2f;
	hand->matrix[2][3] = 2.5f;
	head_yaw (0);
	V_ResetTrackedAim ();
	R_InvalidateStereoReference ();
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	assert (V_TrackedPlayerBase (&base_viewheight));
	assert (R_TrackedHeadEyeHeight (base_viewheight, &eye_height));
	near_value (eye_height, -10 + 1.5f * units);
	assert (V_TrackedHandBodyOffset (0, grip));
	near_value (grip[0], -.5f * units);
	near_value (grip[1], .25f * units);
	near_value (grip[2], eye_height - .3f * units);
	// The hand query may establish the renderer reference before preparation.
	R_PrepareStereoFrame ();
	assert (R_TrackedHeadEyeHeight (base_viewheight, &prepared_eye_height));
	near_value (prepared_eye_height, eye_height);
	R_RestoreStereoView ();
	assert (V_TurnTrackedYaw (90));
	assert (V_TrackedHandBodyOffset (0, grip));
	near_value (grip[0], -.25f * units);
	near_value (grip[1], -.5f * units);
	near_value (grip[2], eye_height - .3f * units);

	V_ResetTrackedAim ();
	R_InvalidateStereoReference ();
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	assert (V_TrackedPlayerBase (&base_viewheight));
	test_frame.floor_referenced = 0;
	assert (R_TrackedHeadEyeHeight (base_viewheight, &eye_height));
	near_value (eye_height, base_viewheight + 6);
	assert (V_TrackedHandBodyOffset (0, grip));
	near_value (grip[2], eye_height - .3f * units);
	head->matrix[1][3] = 1.7f;
	hand->matrix[1][3] = 1.4f;
	assert (R_TrackedHeadEyeHeight (base_viewheight, &eye_height));
	near_value (eye_height, base_viewheight + 6 + .2f * units);
	assert (V_TrackedHandBodyOffset (0, grip));
	near_value (grip[2], eye_height - .3f * units);
	// The runtime notification rebases the same renderer-owned reference.
	head->matrix[1][3] = 2.3f;
	hand->matrix[1][3] = 2.0f;
	test_frame.reference_changed = 1;
	assert (V_TrackedHandBodyOffset (0, grip));
	near_value (grip[2], base_viewheight + 6 - .3f * units);
	test_frame.reference_changed = 0;

	hand->hand = 1;
	assert (!V_TrackedHandBodyOffset (0, grip));
	near_value (grip[0], 0);
	near_value (grip[1], 0);
	near_value (grip[2], 0);
	hand->hand = 0;
	hand->matrix[0][3] = NAN;
	assert (!V_TrackedHandBodyOffset (0, grip));
	near_value (grip[0], 0);
	near_value (grip[1], 0);
	near_value (grip[2], 0);
	hand->matrix[0][3] = .75f;
	head->matrix[1][3] = NAN;
	eye_height = 1;
	assert (!R_TrackedHeadEyeHeight (base_viewheight, &eye_height));
	near_value (eye_height, 0);
	assert (!V_TrackedHandBodyOffset (0, grip));
	near_value (grip[0], 0);
	near_value (grip[1], 0);
	near_value (grip[2], 0);
	head->matrix[1][3] = 2.3f;
	assert (!cl.cmd.vr_active && !cl.pendingcmd.vr_active);
	vr_floor_offset.value = -16;
}
static void test_roomscale_eye_anchor (void)
{
	const float units = V_VRUnitsPerMetre ();
	float base_y, anchored_z;
	cls.signon = SIGNONS;
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cls.demoplayback = false;
	cl.paused = false;
	cl.intermission = 0;
	cl.fixangle_time = -1;
	cl.stats[STAT_HEALTH] = 100;
	cl.cmd.vr_active = cl.pendingcmd.vr_active = false;
	chase_active.value = 0;
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	test_frame.focused = test_frame.should_render = test_frame.floor_referenced = 1;
	test_frame.devices[0].valid = 1;
	test_frame.devices[0].matrix[0][3] = 0;
	test_frame.devices[0].matrix[1][3] = 1.7f;
	test_frame.devices[0].matrix[2][3] = 0;
	head_yaw (0);
	VectorCopy (vec3_origin, cl.viewangles);
	V_ResetTrackedAim ();
	R_InvalidateStereoReference ();
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	assert (base_player_view && tracked_aim_ready);
	base_y = r_refdef.vieworg[1];
	R_PrepareStereoFrame ();
	R_RestoreStereoView ();
	// Before command activation, the existing camera remains positional.
	test_frame.devices[0].matrix[0][3] = .2f;
	head_yaw (0);
	V_UpdateTrackedAim ();
	assert (!V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y - .2f * units);
	R_RestoreStereoView ();
	// A preview can own the body step before the first tagged command is sent.
	cl.pendingcmd.vr_active = true;
	assert (V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	anchored_z = r_refdef.vieworg[2];
	R_RestoreStereoView ();
	// After send clears pending, the last private command retains the anchor.
	cl.pendingcmd.vr_active = false;
	cl.cmd.vr_active = true;
	test_frame.devices[0].matrix[0][3] = .3f; // blocked step: body stays put
	test_frame.devices[0].matrix[1][3] = 1.8f;
	head_yaw (0);
	V_UpdateTrackedAim ();
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	near_value (r_refdef.vieworg[2], anchored_z + .1f * units);
	R_RestoreStereoView ();
	// An accepted body step moves the eye with the player, without adding the
	// same raw HMD displacement a second time.
	cl.entities[cl.viewentity].origin[1] += 5;
	V_CalcRefdef ();
	base_y = r_refdef.vieworg[1];
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	R_RestoreStereoView ();
	R_InvalidateStereoReference ();
	V_UpdateTrackedAim ();
	test_frame.devices[0].matrix[0][3] = .4f;
	head_yaw (0);
	V_UpdateTrackedAim ();
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	R_RestoreStereoView ();
	// A lost input focus can clear fresh command tags while the runtime still
	// renders a valid head pose. Never restore the historical translation.
	cl.cmd.vr_active = false;
	test_frame.focused = 0;
	test_frame.devices[0].matrix[0][3] = .45f;
	head_yaw (0);
	assert (V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	R_RestoreStereoView ();
	test_frame.focused = 1;
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	assert (V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y);
	R_RestoreStereoView ();
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	// Public peers never acquire a body-motion anchor from a stale private cmd.
	test_frame.devices[0].matrix[0][3] = .5f;
	head_yaw (0);
	V_UpdateTrackedAim ();
	cl.protocol_qsvr = 0;
	assert (!V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], base_y - .1f * units);
	R_RestoreStereoView ();
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	assert (!V_TrackedBodyOwnsRoomscale ());
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	cl.cmd.vr_active = true;
	sv.active = true;
	cl.protocol_qsvr = 0;
	assert (!V_TrackedBodyOwnsRoomscale ()); // local server is not private-ready
	sv.active = false;
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	assert (V_TrackedBodyOwnsRoomscale ());
	chase_active.value = 1;
	V_CalcRefdef ();
	assert (!V_TrackedBodyOwnsRoomscale ());
	chase_active.value = 0;
	cl.cmd.vr_active = false;
}

enum sixdof_axis
{
	SIXDOF_X, SIXDOF_Y, SIXDOF_Z,
	SIXDOF_YAW, SIXDOF_PITCH, SIXDOF_ROLL
};

typedef struct
{
	const char *name;
	enum sixdof_axis axis;
	float sign;
} sixdof_case_t;

static void sixdof_near (const char *case_name, const char *quantity,
	int component, float actual, float expected)
{
	if (!(fabsf (actual - expected) < .003f))
		fprintf (stderr, "six-axis case %s: %s[%d] actual %.6f expected %.6f\n",
			case_name, quantity, component, actual, expected);
	assert (fabsf (actual - expected) < .003f);
}

static void sixdof_expected_basis (enum sixdof_axis axis, float degrees,
	vec3_t forward, vec3_t right, vec3_t up)
{
	const float radians = degrees * (float)(M_PI / 180.0);
	const float c = cosf (radians), s = sinf (radians);
	if (axis == SIXDOF_YAW)
	{
		forward[0] = c; forward[1] = s; forward[2] = 0;
		right[0] = s; right[1] = -c; right[2] = 0;
		up[0] = 0; up[1] = 0; up[2] = 1;
	}
	else if (axis == SIXDOF_PITCH)
	{
		forward[0] = c; forward[1] = 0; forward[2] = s;
		right[0] = 0; right[1] = -1; right[2] = 0;
		up[0] = -s; up[1] = 0; up[2] = c;
	}
	else if (axis == SIXDOF_ROLL)
	{
		forward[0] = 1; forward[1] = 0; forward[2] = 0;
		right[0] = 0; right[1] = -c; right[2] = s;
		up[0] = 0; up[1] = s; up[2] = c;
	}
	else
	{
		forward[0] = 1; forward[1] = 0; forward[2] = 0;
		right[0] = 0; right[1] = -1; right[2] = 0;
		up[0] = 0; up[1] = 0; up[2] = 1;
	}
}

static void sixdof_camera_setup (vec3_t quiet_center, float game_yaw)
{
	vrxr_device_t *head = &test_frame.devices[0];
	memset (&test_frame.devices[1], 0, 2 * sizeof (test_frame.devices[0]));
	memset (head, 0, sizeof (*head));
	head->valid = head->tracked = 1;
	head->kind = VRXR_DEVICE_HEAD;
	head->hand = -1;
	test_frame.should_render = test_frame.focused = 1;
	test_frame.reference_changed = 0;
	test_frame.floor_referenced = 0;
	for (int eye = 0; eye < 2; ++eye)
	{
		test_frame.views[eye].left = -1.05f;
		test_frame.views[eye].right = 1.0f;
		test_frame.views[eye].down = -.85f;
		test_frame.views[eye].up = 1.1f;
	}
	set_rigid_pose (0, 0, 0, 0, 0, 0);
	vrxr_device_t *hand = &test_frame.devices[2];
	pose_matrix (hand->matrix, 35, 0, 0, .3f, 1.1f, -.2f);
	hand->valid = hand->tracked = 1;
	hand->kind = VRXR_DEVICE_HAND;
	hand->hand = 1;

	cls.signon = SIGNONS;
	cls.demoplayback = false;
	cl.protocol_qsvr = 0;
	cl.paused = false;
	cl.intermission = 0;
	cl.fixangle_time = -1;
	cl.cmd.vr_active = cl.pendingcmd.vr_active = false;
	cl.stats[STAT_HEALTH] = 100;
	cl.stats[STAT_VIEWHEIGHT] = 22;
	cl.velocity[0] = cl.velocity[1] = cl.velocity[2] = 0;
	VectorCopy (vec3_origin, cl.viewangles);
	cl.viewangles[YAW] = game_yaw;
	chase_active.value = 0;
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	vr_world_scale.value = 1;
	vr_floor_offset.value = -16;
	V_ResetTrackedAim ();
	R_InvalidateStereoReference ();
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	assert (!V_TrackedBodyOwnsRoomscale ());
	R_PrepareStereoFrame ();
	VectorCopy (r_refdef.vieworg, quiet_center);
	R_RestoreStereoView ();
}

static void test_sixdof_prepared_camera_case (const sixdof_case_t *test)
{
	const float units = V_VRUnitsPerMetre ();
	const float delta = test->sign * .2f;
	const float angle = test->sign * 15.f;
	float x = 0, y = 0, z = 0, pitch = 0, yaw = 0, roll = 0;
	vec3_t quiet_center, expected_center, expected_forward, expected_right, expected_up;
	vec3_t before_origin, before_angles, actual_forward, actual_right, actual_up;
	usercmd_t command;

	if (test->axis == SIXDOF_X) x = delta;
	else if (test->axis == SIXDOF_Y) y = delta;
	else if (test->axis == SIXDOF_Z) z = delta;
	else if (test->axis == SIXDOF_YAW) yaw = angle;
	else if (test->axis == SIXDOF_PITCH) pitch = angle;
	else roll = angle;

	fprintf (stderr, "six-axis camera case: %s\n", test->name);
	sixdof_camera_setup (quiet_center, 0);
	set_rigid_pose (x, y, z, pitch, yaw, roll);
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	assert (!V_TrackedBodyOwnsRoomscale ());
	VectorCopy (r_refdef.vieworg, before_origin);
	VectorCopy (r_refdef.viewangles, before_angles);
	CL_BaseMove (&command);
	vec3_t hand_aim;
	assert (V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, 1, hand_aim));
	for (int axis = 0; axis < 3; ++axis)
		sixdof_near (test->name, "command", axis, command.viewangles[axis], cl.viewangles[axis]);
	assert (fabsf (hand_aim[YAW] - command.viewangles[YAW]) > 1.f);

	VectorCopy (quiet_center, expected_center);
	if (test->axis == SIXDOF_X)
		expected_center[1] -= delta * units;
	else if (test->axis == SIXDOF_Y)
		expected_center[2] += delta * units;
	else if (test->axis == SIXDOF_Z)
		expected_center[0] -= delta * units;
	sixdof_expected_basis (test->axis, angle, expected_forward, expected_right, expected_up);
	R_PrepareStereoFrame ();
	for (int axis = 0; axis < 3; ++axis)
		sixdof_near (test->name, "center", axis, r_refdef.vieworg[axis], expected_center[axis]);
	AngleVectors (r_refdef.viewangles, actual_forward, actual_right, actual_up);
	for (int axis = 0; axis < 3; ++axis)
	{
		sixdof_near (test->name, "forward", axis, actual_forward[axis], expected_forward[axis]);
		sixdof_near (test->name, "right", axis, actual_right[axis], expected_right[axis]);
		sixdof_near (test->name, "up", axis, actual_up[axis], expected_up[axis]);
	}
	for (int eye = 0; eye < 2; ++eye)
		for (int axis = 0; axis < 3; ++axis)
		{
			const float side = eye ? 1.f : -1.f;
			const float expected = expected_center[axis] + side * .032f * units * expected_right[axis];
			sixdof_near (test->name, eye ? "right-eye" : "left-eye", axis,
				r_stereo_origins[eye][axis], expected);
		}
	R_RestoreStereoView ();
	for (int axis = 0; axis < 3; ++axis)
	{
		sixdof_near (test->name, "restored-center", axis, r_refdef.vieworg[axis], before_origin[axis]);
		sixdof_near (test->name, "restored-angle", axis, r_refdef.viewangles[axis], before_angles[axis]);
	}
}

static void test_sixdof_prepared_camera (void)
{
	static const sixdof_case_t cases[] = {
		{"translation X +delta", SIXDOF_X, 1}, {"translation X -delta", SIXDOF_X, -1},
		{"translation Y +delta", SIXDOF_Y, 1}, {"translation Y -delta", SIXDOF_Y, -1},
		{"translation Z +delta", SIXDOF_Z, 1}, {"translation Z -delta", SIXDOF_Z, -1},
		{"yaw +angle", SIXDOF_YAW, 1}, {"yaw -angle", SIXDOF_YAW, -1},
		{"pitch +angle", SIXDOF_PITCH, 1}, {"pitch -angle", SIXDOF_PITCH, -1},
		{"roll +angle", SIXDOF_ROLL, 1}, {"roll -angle", SIXDOF_ROLL, -1}
	};
	for (unsigned int i = 0; i < sizeof (cases) / sizeof (cases[0]); ++i)
		test_sixdof_prepared_camera_case (&cases[i]);
}

/* Independent rigid-rotation oracle: Hamilton products, without pose-angle
 * extraction or the renderer's tracking-basis compensation. */
typedef struct { double w, x, y, z; } camera_oracle_quat_t;

static camera_oracle_quat_t camera_oracle_product (camera_oracle_quat_t a,
	camera_oracle_quat_t b)
{
	const camera_oracle_quat_t result = {
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w
	};
	return result;
}

static camera_oracle_quat_t camera_oracle_axis (int axis, double degrees)
{
	const double half = degrees * M_PI / 360.0, s = sin (half);
	const camera_oracle_quat_t result = {
		cos (half), axis == 0 ? s : 0, axis == 1 ? s : 0, axis == 2 ? s : 0
	};
	return result;
}

static void camera_oracle_vector (camera_oracle_quat_t q,
	double x, double y, double z, vec3_t quake)
{
	const camera_oracle_quat_t point = {0, x, y, z};
	const camera_oracle_quat_t inverse = {q.w, -q.x, -q.y, -q.z};
	const camera_oracle_quat_t rotated = camera_oracle_product (
		camera_oracle_product (q, point), inverse);
	/* XR right/up/back become Quake -Y/+Z/-X at game yaw zero. */
	quake[0] = (float)-rotated.z;
	quake[1] = (float)-rotated.x;
	quake[2] = (float)rotated.y;
}

static void test_sixdof_composed_camera (void)
{
	const char *name = "composed XYZ/yaw/pitch/roll at game yaw 37";
	const camera_oracle_quat_t body = camera_oracle_axis (1, 37);
	const camera_oracle_quat_t head = camera_oracle_product (
		camera_oracle_product (camera_oracle_axis (1, 21), camera_oracle_axis (0, -13)),
		camera_oracle_axis (2, 9));
	const camera_oracle_quat_t view = camera_oracle_product (body, head);
	vec3_t quiet_center, expected_center, offset, forward, right, up;
	vec3_t actual_forward, actual_right, actual_up, before_origin, before_angles;
	usercmd_t command;

	fprintf (stderr, "six-axis camera case: %s\n", name);
	sixdof_camera_setup (quiet_center, 37);
	const float units = V_VRUnitsPerMetre ();
	set_rigid_pose (.18f, -.07f, -.23f, -13, 21, 9);
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	assert (!V_TrackedBodyOwnsRoomscale ());
	VectorCopy (r_refdef.vieworg, before_origin);
	VectorCopy (r_refdef.viewangles, before_angles);
	CL_BaseMove (&command);
	sixdof_near (name, "command-yaw", YAW, command.viewangles[YAW], 37);
	camera_oracle_vector (body, .18, -.07, -.23, offset);
	camera_oracle_vector (view, 0, 0, -1, forward);
	camera_oracle_vector (view, 1, 0, 0, right);
	camera_oracle_vector (view, 0, 1, 0, up);
	for (int axis = 0; axis < 3; ++axis)
		expected_center[axis] = quiet_center[axis] + offset[axis] * units;
	R_PrepareStereoFrame ();
	AngleVectors (r_refdef.viewangles, actual_forward, actual_right, actual_up);
	for (int axis = 0; axis < 3; ++axis)
	{
		sixdof_near (name, "center", axis, r_refdef.vieworg[axis], expected_center[axis]);
		sixdof_near (name, "forward", axis, actual_forward[axis], forward[axis]);
		sixdof_near (name, "right", axis, actual_right[axis], right[axis]);
		sixdof_near (name, "up", axis, actual_up[axis], up[axis]);
		for (int eye = 0; eye < 2; ++eye)
			sixdof_near (name, eye ? "right-eye" : "left-eye", axis, r_stereo_origins[eye][axis],
				expected_center[axis] + (eye ? .032f : -.032f) * units * right[axis]);
	}
	R_RestoreStereoView ();
	for (int axis = 0; axis < 3; ++axis)
	{
		sixdof_near (name, "restored-center", axis, r_refdef.vieworg[axis], before_origin[axis]);
		sixdof_near (name, "restored-angle", axis, r_refdef.viewangles[axis], before_angles[axis]);
	}
	fprintf (stderr, "six-axis composed quaternion oracle passed\n");
}

static void check_canonical_body_queries (const char *name, const vec3_t expected)
{
	for (int repeat = 0; repeat < 3; ++repeat)
	{
		vec3_t offset;
		assert (R_TrackedHeadBodyOffset (offset));
		for (int axis = 0; axis < 3; ++axis)
			sixdof_near (name, "canonical-offset", axis, offset[axis], expected[axis]);
	}
}

static void check_owned_camera_sample (const char *name, const vec3_t body_center,
	float fresh_x, float fresh_z)
{
	const float units = V_VRUnitsPerMetre ();
	const vec3_t delta = {-fresh_z * units, -fresh_x * units, 0};
	vec3_t before_origin, before_angles, before_body;
	usercmd_t before_command;
	VectorCopy (r_refdef.vieworg, before_origin);
	VectorCopy (r_refdef.viewangles, before_angles);
	VectorCopy (cl.entities[cl.viewentity].origin, before_body);
	memcpy (&before_command, &cl.cmd, sizeof (before_command));
	check_canonical_body_queries (name, delta);
	R_PrepareStereoFrame ();
	for (int axis = 0; axis < 3; ++axis)
	{
		const float center = body_center[axis] + delta[axis];
		sixdof_near (name, "center", axis, r_refdef.vieworg[axis], center);
		sixdof_near (name, "fixed-orientation", axis, r_refdef.viewangles[axis], 0);
		for (int eye = 0; eye < 2; ++eye)
			sixdof_near (name, eye ? "right-eye" : "left-eye", axis, r_stereo_origins[eye][axis],
				center + (axis == 1 ? (eye ? -.032f : .032f) * units : 0));
	}
	check_canonical_body_queries (name, delta);
	R_RestoreStereoView ();
	check_canonical_body_queries (name, delta);
	assert (!memcmp (&before_command, &cl.cmd, sizeof (before_command)));
	for (int axis = 0; axis < 3; ++axis)
	{
		sixdof_near (name, "retained-center", axis, r_refdef.vieworg[axis], before_origin[axis]);
		sixdof_near (name, "retained-angle", axis, r_refdef.viewangles[axis], before_angles[axis]);
		sixdof_near (name, "fixed-body", axis, cl.entities[cl.viewentity].origin[axis], before_body[axis]);
	}
	fprintf (stderr, "owned camera/query case passed: %s\n", name);
}

static void test_paused_private_camera_fresh_delta (void)
{
	const char *name = "paused private owner: head X +0.2, Z -0.15";
	const vec3_t fixed_body = {100, 200, 300};
	vec3_t quiet_center, rendered_center, rendered_angles, rendered_eyes[2];
	vec3_t retained_origin, retained_angles, actual_center, actual_angles, actual_eyes[2];
	vec3_t expected_center, fresh_delta;
	usercmd_t native_command;

	fprintf (stderr, "six-axis camera case: %s\n", name);
	VectorCopy (fixed_body, cl.entities[cl.viewentity].origin);
	sixdof_camera_setup (quiet_center, 0);
	const float units = V_VRUnitsPerMetre ();
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.cmd.vr_active = true;
	VectorCopy (cl.viewangles, cl.cmd.viewangles);
	assert (V_TrackedBodyOwnsRoomscale ());
	set_rigid_pose (.3f, 0, 0, 0, 0, 0);
	assert (!cl.paused);
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	VectorCopy (r_refdef.vieworg, retained_origin);
	VectorCopy (r_refdef.viewangles, retained_angles);
	R_PrepareStereoFrame ();
	VectorCopy (r_refdef.vieworg, rendered_center);
	VectorCopy (r_refdef.viewangles, rendered_angles);
	for (int axis = 0; axis < 3; ++axis)
		sixdof_near (name, "unpaused-owned-center", axis, rendered_center[axis], quiet_center[axis]);
	for (int eye = 0; eye < 2; ++eye)
		VectorCopy (r_stereo_origins[eye], rendered_eyes[eye]);
	R_RestoreStereoView ();
	memcpy (&native_command, &cl.cmd, sizeof (native_command));

	/* Retain the actually rendered unpaused base. Only the fresh physical
	 * motion is presentation motion while the body/native command is frozen. */
	cl.paused = true;
	set_rigid_pose (.5f, 0, -.15f, 0, 0, 0);
	V_UpdateTrackedAim ();
	assert (V_TrackedBodyOwnsRoomscale ());
	fresh_delta[0] = .15f * units;
	fresh_delta[1] = -.2f * units;
	fresh_delta[2] = 0;
	check_canonical_body_queries (name, fresh_delta);
	R_PrepareStereoFrame (); // deliberately no V_CalcRefdef while paused
	VectorCopy (r_refdef.vieworg, actual_center);
	VectorCopy (r_refdef.viewangles, actual_angles);
	for (int eye = 0; eye < 2; ++eye)
		VectorCopy (r_stereo_origins[eye], actual_eyes[eye]);
	check_canonical_body_queries (name, fresh_delta);
	R_RestoreStereoView ();
	check_canonical_body_queries (name, fresh_delta);
	assert (!memcmp (&cl.cmd, &native_command, sizeof (native_command)));
	fresh_delta[0] = .15f * units;
	fresh_delta[1] = -.2f * units;
	fresh_delta[2] = 0;
	for (int axis = 0; axis < 3; ++axis)
	{
		expected_center[axis] = rendered_center[axis] + fresh_delta[axis];
		sixdof_near (name, "fixed-body", axis, cl.entities[cl.viewentity].origin[axis], fixed_body[axis]);
		sixdof_near (name, "fixed-native-aim", axis, cl.viewangles[axis], native_command.viewangles[axis]);
		sixdof_near (name, "fixed-orientation", axis, actual_angles[axis], rendered_angles[axis]);
		sixdof_near (name, "retained-center", axis, r_refdef.vieworg[axis], retained_origin[axis]);
		sixdof_near (name, "retained-angle", axis, r_refdef.viewangles[axis], retained_angles[axis]);
	}
	fprintf (stderr, "paused fresh delta: actual (%.6f, %.6f, %.6f), expected (%.6f, %.6f, %.6f)\n",
		actual_center[0] - rendered_center[0], actual_center[1] - rendered_center[1],
		actual_center[2] - rendered_center[2], fresh_delta[0], fresh_delta[1], fresh_delta[2]);
	fprintf (stderr, "paused center: actual (%.6f, %.6f, %.6f), expected (%.6f, %.6f, %.6f)\n",
		actual_center[0], actual_center[1], actual_center[2],
		expected_center[0], expected_center[1], expected_center[2]);
	for (int axis = 0; axis < 3; ++axis)
	{
		sixdof_near (name, "fresh-center", axis, actual_center[axis], expected_center[axis]);
		for (int eye = 0; eye < 2; ++eye)
			sixdof_near (name, eye ? "right-eye" : "left-eye", axis, actual_eyes[eye][axis],
				rendered_eyes[eye][axis] + fresh_delta[axis]);
	}
	/* Repeated paused queries and camera preparations must not advance the
	 * last unpaused sample (X=.3, Z=0). */
	check_owned_camera_sample ("repeat original paused sample", rendered_center, .2f, -.15f);
	set_rigid_pose (.6f, 0, -.2f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("later paused delta still from unpaused sample", rendered_center, .3f, -.2f);

	/* Temporary unavailable tracking must preserve that sample. */
	set_rigid_pose (9, 0, -8, 0, 0, 0);
	test_frame.devices[0].valid = false;
	V_UpdateTrackedAim ();
	vec3_t unavailable = {1, 2, 3};
	assert (!R_TrackedHeadBodyOffset (unavailable));
	for (int axis = 0; axis < 3; ++axis)
		near_value (unavailable[axis], 0);
	R_PrepareStereoFrame ();
	R_RestoreStereoView ();
	test_frame.devices[0].valid = true;
	set_rigid_pose (.7f, 0, -.25f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("unusable head recovery preserves baseline", rendered_center, .4f, -.25f);

	/* A temporary unavailable player base is not retirement either. */
	base_player_view = false;
	set_rigid_pose (.8f, 0, -.3f, 0, 0, 0);
	assert (R_TrackedHeadBodyOffset (unavailable));
	R_PrepareStereoFrame ();
	R_RestoreStereoView ();
	base_player_view = true;
	set_rigid_pose (.9f, 0, -.35f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("player base recovery preserves baseline", rendered_center, .6f, -.35f);

	R_InvalidateStereoReference ();
	set_rigid_pose (1.2f, 0, -.5f, 0, 0, 0);
	V_UpdateTrackedAim ();
	test_frame.reference_changed = 1;
	check_owned_camera_sample ("paused reference reset seeds first sample", rendered_center, 0, 0);
	test_frame.reference_changed = 0;
	set_rigid_pose (1.4f, 0, -.65f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("fresh motion after paused reference seed", rendered_center, .2f, -.15f);

	vulkan_globals.stereo_active = false;
	assert (!R_TrackedHeadBodyOffset (unavailable));
	R_PrepareStereoFrame (); // no-frame boundary retires the shared sample
	R_RestoreStereoView ();
	vulkan_globals.stereo_active = true;
	set_rigid_pose (2, 0, -1, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("no-frame retirement seeds new paused sample", rendered_center, 0, 0);
	set_rigid_pose (2.1f, 0, -1.2f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("fresh motion after no-frame retirement", rendered_center, .1f, -.2f);

	cl.protocol_qsvr = 0;
	set_rigid_pose (2.2f, 0, -1.3f, 0, 0, 0);
	V_UpdateTrackedAim ();
	assert (R_TrackedHeadBodyOffset (unavailable));
	R_PrepareStereoFrame ();
	R_RestoreStereoView ();
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	assert (V_TrackedBodyOwnsRoomscale ());
	set_rigid_pose (2.5f, 0, -1.5f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("protocol retirement seeds new private paused sample", rendered_center, 0, 0);
	set_rigid_pose (2.65f, 0, -1.6f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("fresh motion after private paused reseed", rendered_center, .15f, -.1f);
	for (int retirement = 0; retirement < 2; ++retirement)
	{
		cls.demoplayback = retirement == 0;
		cl.intermission = retirement == 1;
		R_PrepareStereoFrame ();
		R_RestoreStereoView ();
		cls.demoplayback = false;
		cl.intermission = 0;
		const float seed_x = 3.f + retirement;
		set_rigid_pose (seed_x, 0, -2, 0, 0, 0);
		V_UpdateTrackedAim ();
		check_owned_camera_sample (retirement ? "intermission retirement reseeds" : "demo retirement reseeds",
			rendered_center, 0, 0);
		set_rigid_pose (seed_x + .1f, 0, -2.1f, 0, 0, 0);
		V_UpdateTrackedAim ();
		check_owned_camera_sample (retirement ? "fresh motion after intermission" : "fresh motion after demo",
			rendered_center, .1f, -.1f);
	}

	/* A client/map reset can remain private and paused throughout: neither a
	 * no-frame render nor a public-protocol transition may be needed to retire
	 * the previous camera sample. Rebuild the new paused client base once. */
	assert (cl.paused && cl.protocol_qsvr == QSVR_PROTOCOL_PINNED && cl.cmd.vr_active);
	assert (tracked_body_anchor && stereo_body_horizontal_valid);
	assert (GL_OpenXRFrame () == &test_frame);
	V_ResetTrackedAim ();
	assert (!tracked_body_anchor && !stereo_body_horizontal_valid);
	assert (cl.paused && cl.protocol_qsvr == QSVR_PROTOCOL_PINNED && cl.cmd.vr_active);
	assert (GL_OpenXRFrame () == &test_frame);
	set_rigid_pose (6, 0, -3, 0, 0, 0);
	V_UpdateTrackedAim ();
	V_SetupFrame ();
	check_owned_camera_sample ("direct aim reset seeds new private paused client", rendered_center, 0, 0);
	assert (tracked_body_anchor && stereo_body_horizontal_valid);
	set_rigid_pose (6.2f, 0, -3.15f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("fresh paused motion after direct aim reset", rendered_center, .2f, -.15f);

	/* Resuming explicitly retires paused presentation displacement. Gameplay
	 * returns to the collision-resolved body; continuity across resume is not
	 * the contract. Only unpaused camera preparation may publish its sample. */
	cl.paused = false;
	set_rigid_pose (2.8f, 0, -1.7f, 0, 0, 0);
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	check_owned_camera_sample ("resume retires paused view to collision body base", rendered_center, 0, 0);
	/* Query a later unpaused pose without rendering it: a read must not
	 * replace the camera's just-published X=2.8/Z=-1.7 sample. */
	set_rigid_pose (2.85f, 0, -1.75f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_canonical_body_queries ("unpaused query cannot publish a newer sample", vec3_origin);
	cl.paused = true;
	set_rigid_pose (2.9f, 0, -1.8f, 0, 0, 0);
	V_UpdateTrackedAim ();
	check_owned_camera_sample ("paused motion uses newly rendered resume sample", rendered_center, .1f, -.1f);
	assert (!memcmp (&cl.cmd, &native_command, sizeof (native_command)));
	for (int axis = 0; axis < 3; ++axis)
		sixdof_near (name, "fixed-body-final", axis, cl.entities[cl.viewentity].origin[axis], fixed_body[axis]);
	cl.paused = false;
}

static void test_predicted_valid_camera_snapshot (void)
{
	static const struct
	{
		const char *name;
		qboolean paused;
		float head_x, head_z, fresh_x, fresh_z;
	} samples[] = {
		{"predicted unpaused sample publishes snapshot", false, .3f, 0, 0, 0},
		{"predicted paused camera follows fresh delta", true, .5f, -.15f, .2f, -.15f},
		{"predicted paused render does not advance snapshot", true, .6f, -.2f, .3f, -.2f},
		{"predicted resume publishes new body sample", false, .7f, -.3f, 0, 0},
		{"predicted repause follows resumed sample", true, .8f, -.4f, .1f, -.1f}
	};
	vec3_t body_center, fixed_body;
	usercmd_t native_command;
	sixdof_camera_setup (body_center, 0);
	VectorCopy (cl.entities[cl.viewentity].origin, fixed_body);
	test_frame.devices[0].tracked = false;
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.cmd.vr_active = true;
	VectorCopy (cl.viewangles, cl.cmd.viewangles);
	memcpy (&native_command, &cl.cmd, sizeof (native_command));
	const float units = V_VRUnitsPerMetre ();
	for (unsigned int sample = 0; sample < sizeof (samples) / sizeof (samples[0]); ++sample)
	{
		const char *name = samples[sample].name;
		vec3_t retained_origin, retained_angles;
		cl.paused = samples[sample].paused;
		set_rigid_pose (samples[sample].head_x, 0, samples[sample].head_z, 0, 0, 0);
		assert (test_frame.devices[0].valid && !test_frame.devices[0].tracked);
		assert (V_UseTrackedView ());
		V_UpdateTrackedAim ();
		if (!cl.paused)
			V_CalcRefdef ();
		assert (V_TrackedBodyOwnsRoomscale ());
		VectorCopy (r_refdef.vieworg, retained_origin);
		VectorCopy (r_refdef.viewangles, retained_angles);
		/* Predicted render validity admits the camera sample. The canonical
		 * body query still rejects an untracked head before/after rendering. */
		for (int boundary = 0; boundary < 3; ++boundary)
		{
			if (boundary == 1)
			{
				R_PrepareStereoFrame ();
				const vec3_t delta = {-samples[sample].fresh_z * units, -samples[sample].fresh_x * units, 0};
				for (int axis = 0; axis < 3; ++axis)
				{
					const float center = body_center[axis] + delta[axis];
					sixdof_near (name, "center", axis, r_refdef.vieworg[axis], center);
					sixdof_near (name, "orientation", axis, r_refdef.viewangles[axis], 0);
					for (int eye = 0; eye < 2; ++eye)
						sixdof_near (name, eye ? "right-eye" : "left-eye", axis, r_stereo_origins[eye][axis],
							center + (axis == 1 ? (eye ? -.032f : .032f) * units : 0));
				}
			}
			else if (boundary == 2)
				R_RestoreStereoView ();
			vec3_t rejected_offset = {1, 2, 3};
			assert (!R_TrackedHeadBodyOffset (rejected_offset));
			for (int axis = 0; axis < 3; ++axis)
				sixdof_near (name, "strict-query-zero", axis, rejected_offset[axis], 0);
		}
		assert (!memcmp (&cl.cmd, &native_command, sizeof (native_command)));
		for (int axis = 0; axis < 3; ++axis)
		{
			sixdof_near (name, "restored-center", axis, r_refdef.vieworg[axis], retained_origin[axis]);
			sixdof_near (name, "restored-angle", axis, r_refdef.viewangles[axis], retained_angles[axis]);
			sixdof_near (name, "fixed-body", axis, cl.entities[cl.viewentity].origin[axis], fixed_body[axis]);
		}
		fprintf (stderr, "predicted valid camera case passed: %s\n", name);
	}
	cl.paused = false;
	test_frame.devices[0].tracked = true;
}

int main (void)
{
	vr_world_scale.value = 1;
	vr_floor_offset.value = -16;
	const float default_units = 1.f / (1.5f * .0254f);
	vulkan_globals.stereo_active = true;
	test_frame.should_render = 1;
	r_refdef.vieworg[0] = 100;
	r_refdef.vieworg[1] = 200;
	r_refdef.vieworg[2] = 300;
	test_frame.devices[0].valid = 1;
	for (int k = 0; k < 3; ++k)
		test_frame.devices[0].matrix[k][k] = 1;
	for (int eye = 0; eye < 2; ++eye)
	{
		for (int k = 0; k < 3; ++k)
			test_frame.views[eye].matrix[k][k] = 1;
		test_frame.views[eye].matrix[0][3] = eye ? .032f : -.032f;
		test_frame.views[eye].left = -1;
		test_frame.views[eye].right = 1;
		test_frame.views[eye].down = -1;
		test_frame.views[eye].up = 1;
	}
	R_PrepareStereoFrame ();
	near_value (r_stereo_origins[0][1], 200 + .032f * default_units);
	near_value (r_stereo_origins[1][1], 200 - .032f * default_units);
	restore ();
	// Repeated paused-camera rendering must not accumulate head translation.
	test_frame.devices[0].matrix[0][3] = .25f;
	for (int frame = 0; frame < 3; ++frame)
	{
		R_PrepareStereoFrame ();
		near_value (r_refdef.vieworg[1], 200 - .25f * default_units);
		restore ();
	}
	// Reference notification is consumed on a skipped frame, before preparation.
	R_InvalidateStereoReference ();
	test_frame.reference_changed = 0;
	test_frame.devices[0].matrix[0][3] = 3;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[1], 200);
	// Outstanding old GPU data must be drained before abandoning this begin.
	gpu_pending = 1;
	in_update_screen = true;
	SCR_AbortXRFrame ();
	assert (gpu_drains == 1 && xr_aborts == 1 && !in_update_screen);
	restore ();
	vulkan_globals.stereo_active = false;
	SCR_AbortXRFrame ();
	assert (gpu_drains == 1 && xr_aborts == 1);

	// Run the real donor view preparation with inherited comfort controls.
	entity_t player = {0};
	qmodel_t world = {0};
	cl.worldmodel = &world;
	cl.entities = &player;
	cl.stats[STAT_VIEWHEIGHT] = 22;
	cl.stats[STAT_HEALTH] = 100;
	cl_bobcycle.value = .6f;
	cl_bobup.value = .5f;
	cl_bob.value = .02f;
	cl_rollangle.value = 2;
	cl_rollspeed.value = 200;
	cl.velocity[1] = 200;
	cl.time = .1;
	host_frametime = .01;
	assert (V_CalcBob () != 0);
	assert (V_CalcRoll (vec3_origin, cl.velocity) != 0);
	vulkan_globals.stereo_active = true;
	near_value (V_CalcBob (), 0);
	near_value (V_CalcRoll (vec3_origin, cl.velocity), 0);
	test_frame.devices[0].matrix[0][3] = 0;
	test_frame.devices[0].matrix[1][3] = 1.7f;
	for (int eye = 0; eye < 2; ++eye)
		test_frame.views[eye].matrix[1][3] = 1.7f;
	test_frame.floor_referenced = 1;
	R_InvalidateStereoReference ();
	V_CalcRefdef ();
	const float bias = 1.f / 32.f;
	for (int pause_frame = 0; pause_frame < 3; ++pause_frame)
	{
		R_PrepareStereoFrame ();
		near_value (r_refdef.vieworg[2], bias + 1.7f * default_units - 16);
		R_RestoreStereoView ();
	}
	// Updated server viewheight cannot corrupt a paused, older view base.
	cl.stats[STAT_VIEWHEIGHT] = 8;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], bias + 1.7f * default_units - 16);
	R_RestoreStereoView ();
	// Crouching and worldscale move height; floor offset stays in Quake units.
	test_frame.devices[0].matrix[1][3] = 1.2f;
	for (int eye = 0; eye < 2; ++eye)
		test_frame.views[eye].matrix[1][3] = 1.2f;
	vr_world_scale.value = 2;
	vr_floor_offset.value = -10;
	r_refdef.viewangles[PITCH] = 60;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], bias + 1.2f * default_units * 2 - 10);
	near_value (r_stereo_radius, .032f * default_units * 2);
	R_RestoreStereoView ();
	// LOCAL keeps relative vertical tracking rather than using its zero as floor.
	test_frame.floor_referenced = 0;
	vr_world_scale.value = 1;
	vr_floor_offset.value = -16;
	R_InvalidateStereoReference ();
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], bias + 22);
	R_RestoreStereoView ();
	test_frame.devices[0].matrix[1][3] += .25f;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], bias + 22 + .25f * default_units);
	R_RestoreStereoView ();
	// Invalid settings never publish non-finite projection units.
	vr_world_scale.value = NAN;
	near_value (V_VRUnitsPerMetre (), default_units);
	vr_world_scale.value = FLT_MAX;
	near_value (V_VRUnitsPerMetre (), default_units);
	vr_world_scale.value = 0;
	near_value (V_VRUnitsPerMetre (), default_units);
	// Death roll and damage kick stay opt-in in a usable XR view.
	VectorCopy (vec3_origin, r_refdef.viewangles);
	cl.stats[STAT_HEALTH] = 0;
	cl.v_dmg_time = .5f;
	cl.v_dmg_roll = 5;
	cl.v_dmg_pitch = 8;
	v_kicktime.value = .5f;
	V_CalcViewRoll ();
	near_value (r_refdef.viewangles[ROLL], 0);
	near_value (r_refdef.viewangles[PITCH], 0);
	assert (cl.v_dmg_time < .5f);
	vr_viewkick.value = 1;
	V_CalcViewRoll ();
	assert (r_refdef.viewangles[ROLL] != 0 && r_refdef.viewangles[PITCH] != 0);
	// Both donor gun-kick modes obey the inherited VR toggle.
	cl.stats[STAT_HEALTH] = 100;
	cl.v_dmg_time = 0;
	cl.punchangle[PITCH] = 8;
	vr_viewkick.value = 0;
	v_gunkick.value = 1;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 0);
	vr_viewkick.value = 1;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 8);
	v_gunkick.value = 2;
	v_punchangles[0][PITCH] = 10;
	v_punchangles_times[0] = .1;
	v_punchangles_times[1] = 0;
	vr_viewkick.value = 0;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 0);
	vr_viewkick.value = 1;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 2);
	// Recoil decays while hidden rather than replaying when the toggle returns.
	vr_viewkick.value = 0;
	v_punchangles[1][PITCH] = v_punchangles[0][PITCH];
	v_punchangles[0][PITCH] = 0;
	for (int frame = 0; frame < 20; ++frame)
		V_CalcRefdef ();
	vr_viewkick.value = 1;
	V_CalcRefdef ();
	near_value (r_refdef.viewangles[PITCH], 0);
	// A prepared chase base remains camera-relative even with a known floor.
	chase_active.value = 1;
	test_frame.floor_referenced = 1;
	vr_world_scale.value = 2;
	R_InvalidateStereoReference ();
	V_CalcRefdef ();
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], 30);
	R_RestoreStereoView ();
	// Paused configuration changes do not reclassify the older chase base.
	chase_active.value = 0;
	R_PrepareStereoFrame ();
	near_value (r_refdef.vieworg[2], 30);
	R_RestoreStereoView ();
	cl.stats[STAT_HEALTH] = 0;
	vulkan_globals.stereo_active = false;
	V_CalcViewRoll ();
	near_value (r_refdef.viewangles[ROLL], 80);

	// Head aiming reaches the real donor command constructor. The view must
	// contain that head rotation once, including with an older paused base.
	vulkan_globals.stereo_active = true;
	cls.signon = SIGNONS;
	cl.fixangle_time = -1;
	cl.stats[STAT_HEALTH] = 100;
	cl.v_dmg_time = 0;
	v_gunkick.value = 0;
	vr_viewkick.value = 0;
	vr_world_scale.value = 1;
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	vr_deadzone.value = 30;
	VectorCopy (vec3_origin, cl.viewangles);
	cl.viewangles[YAW] = 90;
	V_ResetTrackedAim ();
	V_UpdateTrackedAim ();
	V_CalcRefdef ();
	head_yaw (15);
	for (int frame = 0; frame < 3; ++frame)
	{
		V_UpdateTrackedAim ();
		usercmd_t command;
		CL_BaseMove (&command);
		near_value (command.viewangles[YAW], 105);
		R_PrepareStereoFrame ();
		near_value (r_refdef.viewangles[YAW], 105);
		R_RestoreStereoView ();
	}
	// A reference rebase preserves visual/aim separation and input received
	// while the new tracking basis is temporarily unavailable.
	vr_aimmode.value = VR_AIMMODE_MOUSE_MYAW;
	V_TrackedAimModeChanged (NULL);
	cl.viewangles[YAW] = 20;
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 35);
	V_RebaseTrackedAim ();
	test_frame.devices[0].valid = 0;
	cl.viewangles[YAW] += 5;
	V_UpdateTrackedAim ();
	assert (tracked_reference_pending);
	head_yaw (75);
	test_frame.devices[0].valid = 1;
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[YAW], 25);
	near_value (tracked_view_angles[YAW], 40);
	assert (!tracked_reference_pending);
	// Mode changes clear only mode-specific requests, retaining the rebase.
	V_RebaseTrackedAim ();
	vr_aimmode.value = VR_AIMMODE_MOUSE_MYAW_MPITCH;
	V_TrackedAimModeChanged (NULL);
	assert (tracked_reference_pending);
	head_yaw (90);
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 40);
	// Authoritative absolute and relative angles retain their distinct meaning.
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	V_TrackedAimModeChanged (NULL);
	cl.viewangles[YAW] = 120;
	cl.viewangles[PITCH] = 6;
	V_SetTrackedAngles (cl.viewangles);
	cl.fixangle_time = cl.mtime[0];
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[PITCH], 6);
	near_value (tracked_view_angles[PITCH], 0);
	near_value (tracked_view_angles[YAW], 120);
	cl.fixangle_time = -1;
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[PITCH], 0);
	vec3_t server_delta = {0, 30, 0};
	cl.viewangles[YAW] += 30;
	V_TrackedAngleDelta (server_delta);
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[YAW], 150);
	near_value (tracked_view_angles[YAW], 150);
	// Centerview copies visible aim without clearing tracking orientation.
	vr_aimmode.value = VR_AIMMODE_MOUSE_MYAW;
	V_TrackedAimModeChanged (NULL);
	cl.viewangles[YAW] = 10;
	V_UpdateTrackedAim ();
	float before_yaw = tracked_yaw;
	float before_orientation = tracked_previous_orientation[YAW];
	V_StartPitchDrift ();
	near_value (cl.viewangles[YAW], tracked_view_angles[YAW]);
	near_value (tracked_previous_aim[YAW], cl.viewangles[YAW]);
	near_value (tracked_yaw, before_yaw);
	near_value (tracked_previous_orientation[YAW], before_orientation);
	// An accepted server target survives unavailable tracking and takes
	// precedence over a simultaneous reference-origin rebase.
	vr_aimmode.value = VR_AIMMODE_CONTROLLER;
	V_TrackedAimModeChanged (NULL);
	cl.viewangles[YAW] = 270;
	V_SetTrackedAngles (cl.viewangles);
	cl.stats[STAT_WEAPON] = 1;
	V_RequestTrackedServerYaw (270);
	test_frame.devices[0].valid = 0;
	V_UpdateTrackedAim ();
	assert (tracked_server_yaw_pending);
	V_RebaseTrackedAim ();
	head_yaw (135);
	test_frame.devices[0].valid = 1;
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 270);
	near_value (cl.viewangles[YAW], 270);
	assert (!tracked_server_yaw_pending && !tracked_reference_pending);
	// An expired angle lock cannot preserve an obsolete gameplay yaw target.
	V_RequestTrackedServerYaw (90);
	test_frame.devices[0].valid = 0;
	cl.stats[STAT_WEAPON] = 0;
	V_ValidateTrackedServerYaw (); // completed message, even if no frame runs
	cl.stats[STAT_WEAPON] = 1;
	assert (!tracked_server_yaw_pending);
	V_PushTrackedYaw ();
	cl.stats[STAT_WEAPON] = 0;
	V_ValidateTrackedServerYaw ();
	assert (tracked_server_yaw_pending); // standalone setview is distinct
	cl.intermission = 1;
	V_ValidateTrackedServerYaw ();
	assert (!tracked_server_yaw_pending);
	cl.intermission = 0;
	test_frame.devices[0].valid = 1;
	// Locked commands retain their authority while visual head movement
	// accumulates. Unlock publishes it once, including repeated static poses.
	for (int mode = VR_AIMMODE_HEAD_MYAW; mode <= VR_AIMMODE_HEAD_MYAW_MPITCH; ++mode)
	{
		V_ResetTrackedAim ();
		vr_aimmode.value = mode;
		VectorCopy (vec3_origin, cl.viewangles);
		cl.viewangles[YAW] = 90;
		head_yaw (0);
		cl.fixangle_time = -1;
		V_UpdateTrackedAim ();
		cl.fixangle_time = cl.mtime[0];
		for (int sample = 1; sample <= 3; ++sample)
		{
			head_yaw (sample == 1 ? 10 : 20);
			V_UpdateTrackedAim ();
			near_value (cl.viewangles[YAW], 90);
			near_value (tracked_view_angles[YAW], sample == 1 ? 100 : 110);
		}
		cl.fixangle_time = -1;
		for (int sample = 0; sample < 2; ++sample)
		{
			V_UpdateTrackedAim ();
			usercmd_t command;
			CL_BaseMove (&command);
			near_value (command.viewangles[YAW], 110);
			near_value (tracked_view_angles[YAW], 110);
		}
	}
	// Centerview between lock expiry and the next rendered sample must not
	// replay an aim correction it has already copied into command angles.
	V_ResetTrackedAim ();
	vr_aimmode.value = VR_AIMMODE_HEAD_MYAW;
	cl.viewangles[YAW] = 90;
	head_yaw (0);
	V_UpdateTrackedAim ();
	cl.fixangle_time = cl.mtime[0];
	head_yaw (20);
	V_UpdateTrackedAim ();
	near_value (tracked_withheld_aim[YAW], 20);
	cl.fixangle_time = -1;
	V_StartPitchDrift ();
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[YAW], 110);
	near_value (tracked_view_angles[YAW], 110);
	// Real chase orientation follows visual aim while retaining collision
	// position. A paused base receives only the subsequent head-angle delta.
	for (int mode = VR_AIMMODE_MOUSE_MYAW; mode <= VR_AIMMODE_CONTROLLER; mode += 1)
	{
		if (mode == VR_AIMMODE_BLENDED || mode == VR_AIMMODE_BLENDED_NOPITCH)
			continue;
		V_ResetTrackedAim ();
		vr_aimmode.value = mode;
		VectorCopy (vec3_origin, cl.viewangles);
		cl.viewangles[YAW] = 20;
		head_yaw (0);
		V_UpdateTrackedAim ();
		head_yaw (15);
		V_UpdateTrackedAim ();
		chase_active.value = 1;
		R_InvalidateStereoReference ();
		V_CalcRefdef ();
		near_value (r_refdef.vieworg[2], 30);
		near_value (r_refdef.viewangles[YAW], 35);
		const float prepared_yaw = r_refdef.viewangles[YAW];
		R_PrepareStereoFrame ();
		near_value (r_refdef.viewangles[YAW], prepared_yaw);
		R_RestoreStereoView ();
		V_UpdateTrackedAim (); // consume pending origin rebase before motion
		head_yaw (25);
		V_UpdateTrackedAim ();
		R_PrepareStereoFrame ();
		near_value (r_refdef.viewangles[YAW], prepared_yaw + 10);
		near_value (r_refdef.vieworg[2], 30);
		R_RestoreStereoView ();
	}
	chase_active.value = 0;
	// Clearing a client forgets the old world and prepared-base eligibility.
	V_ResetTrackedAim ();
	assert (!base_player_view && !tracked_aim_ready);
	cl.viewangles[YAW] = 180;
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 180);
	// A paused new client must prepare its own base, not subtract the previous
	// map's aim snapshot. Later paused frames keep that newly prepared base.
	cl.paused = true;
	assert (!base_angles_valid);
	V_SetupFrame ();
	assert (base_angles_valid && base_player_view);
	R_PrepareStereoFrame ();
	near_value (fabsf (r_refdef.viewangles[YAW]), 180);
	R_RestoreStereoView ();
	player.origin[0] += 100;
	const float prepared_x = r_refdef.vieworg[0];
	V_SetupFrame ();
	near_value (r_refdef.vieworg[0], prepared_x);
	cl.paused = false;
	cls.demoplayback = true;
	cl.viewangles[YAW] = 77;
	V_UpdateTrackedAim ();
	near_value (cl.viewangles[YAW], 77);
	cls.demoplayback = false;
	// Local turns use one effective basis before submission and are committed
	// once after origin rebasing, in every inherited aim resolver mode.
	test_frame.focused = 1;
	cl.fixangle_time = -1;
	cl.intermission = 0;
	for (int mode = 1; mode <= 7; ++mode)
	{
		vr_aimmode.value = mode;
		VectorCopy (vec3_origin, cl.viewangles);
		head_yaw (0);
		V_ResetTrackedAim ();
		V_UpdateTrackedAim ();
		assert (V_TurnTrackedYaw (-45));
		vec3_t mapped;
		assert (V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HEAD, 0, mapped));
		near_value (mapped[YAW], -45);
		near_value (tracked_previous_orientation[YAW], 0);
		V_RebaseTrackedAim ();
		head_yaw (70);
		assert (!V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HEAD, 0, mapped));
		V_UpdateTrackedAim ();
		near_value (tracked_local_yaw, 0);
		near_value (tracked_view_angles[YAW], -45);
		V_UpdateTrackedAim ();
		near_value (tracked_view_angles[YAW], -45);
	}
	// Relative authority composes with an uncommitted turn; absolute authority
	// discards it. Neither path leaves a sampled movement record eligible.
	int old_invalidations = motion_invalidations;
	assert (V_TurnTrackedYaw (-15));
	vec3_t relative = {0, 30, 0};
	V_TrackedAngleDelta (relative);
	near_value (tracked_local_yaw, -15);
	assert (motion_invalidations > old_invalidations);
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], -30);
	assert (V_TurnTrackedYaw (-45));
	cl.viewangles[YAW] = 90;
	V_SetTrackedAngles (cl.viewangles);
	cl.stats[STAT_WEAPON] = 1;
	V_RequestTrackedServerYaw (90);
	near_value (tracked_local_yaw, 0);
	cl.fixangle_time = cl.mtime[0];
	assert (!V_TurnTrackedYaw (-45));
	V_UpdateTrackedAim ();
	near_value (tracked_view_angles[YAW], 90);
	cl.fixangle_time = -1;
	test_frame.focused = 0;
	assert (!V_TurnTrackedYaw (-45));
	test_hand_body_offset ();
	test_roomscale_eye_anchor ();
	test_sixdof_prepared_camera ();
	test_sixdof_composed_camera ();
	test_paused_private_camera_fresh_delta ();
	test_predicted_valid_camera_snapshot ();
	puts ("Production stereo camera: eye separation, pause restoration, skipped reference invalidation and abort GPU-drain boundary passed");
	puts ("Inherited floor/scale/comfort: floor height, crouch, pitched basis, paused viewheight, LOCAL fallback and desktop gates passed");
	puts ("Head aiming: actual command angles and paused visual view contain one head rotation");
	puts ("Aim transitions: reference loss, mode changes, authoritative angles, centerview, pending cancellation/priority, locked accumulation, real chase and client clear passed");
	puts ("Local turning: effective command basis, all-mode rebase retention, single commit and authority precedence passed");
	puts ("Hand body offsets: shared eye height, LOCAL/reference rebases, axis yaw, invalid poses and pre-camera reference passed");
	puts ("Roomscale camera anchor: pending/sent private body motion removes duplicate horizontal HMD offset; public and vertical paths remain distinct");
	puts ("Prepared six-axis camera: signed XYZ translation and yaw/pitch/roll, rigid IPD eye origins and controller aim separation checked");
}
