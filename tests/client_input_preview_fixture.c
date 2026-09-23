/* Production input construction with engine-global owners supplied by the fixture. */
#include "../Quake/cl_input.c"
#include <assert.h>

client_state_t cl;
client_static_t cls;

/* This fixture isolates native desktop command ownership. The separate XR
 * input/native gameplay probes exercise the production VR adapter. */
qboolean V_TrackedSessionActive (void) { return false; }
void VR_InputApplyPending (usercmd_t *cmd) { (void)cmd; }
static qboolean suppress_uncalibrated_attack;
qboolean VR_InputSuppressUncalibratedAttack (const usercmd_t *cmd)
{
	(void)cmd;
	return suppress_uncalibrated_attack;
}

static kbutton_t *const tracked_keys[] = {
	&in_mlook, &in_klook,
	&in_left, &in_right, &in_forward, &in_back,
	&in_lookup, &in_lookdown, &in_moveleft, &in_moveright,
	&in_strafe, &in_speed, &in_use, &in_jump, &in_attack,
	&in_up, &in_down,
};

typedef struct
{
	kbutton_t keys[countof (tracked_keys)];
	int impulse;
} input_snapshot_t;

typedef struct
{
	usercmd_t pendingcmd;
	usercmd_t cmd;
	usercmd_t movecmds[64];
	int movemessages;
	int ackedmovemessages;
	double move_msec_sample_time;
	double move_msec_fractional_carry;
	qboolean move_msec_sample_valid;
	unsigned long long net_move_msec_generated;
	double time;
	double oldtime;
	double mtime[2];
	vec3_t viewangles;
} persistent_snapshot_t;

static void capture_input (input_snapshot_t *snapshot)
{
	for (unsigned int i = 0; i < countof (tracked_keys); ++i)
		snapshot->keys[i] = *tracked_keys[i];
	snapshot->impulse = in_impulse;
}

static void assert_input_equal (const input_snapshot_t *snapshot)
{
	for (unsigned int i = 0; i < countof (tracked_keys); ++i)
		assert (memcmp (&snapshot->keys[i], tracked_keys[i], sizeof snapshot->keys[i]) == 0);
	assert (snapshot->impulse == in_impulse);
}

static void capture_persistent (persistent_snapshot_t *snapshot)
{
	snapshot->pendingcmd = cl.pendingcmd;
	snapshot->cmd = cl.cmd;
	memcpy (snapshot->movecmds, cl.movecmds, sizeof snapshot->movecmds);
	snapshot->movemessages = cl.movemessages;
	snapshot->ackedmovemessages = cl.ackedmovemessages;
	snapshot->move_msec_sample_time = cl.move_msec_sample_time;
	snapshot->move_msec_fractional_carry = cl.move_msec_fractional_carry;
	snapshot->move_msec_sample_valid = cl.move_msec_sample_valid;
	snapshot->net_move_msec_generated = cl.net_move_msec_generated;
	snapshot->time = cl.time;
	snapshot->oldtime = cl.oldtime;
	snapshot->mtime[0] = cl.mtime[0];
	snapshot->mtime[1] = cl.mtime[1];
	VectorCopy (cl.viewangles, snapshot->viewangles);
}

static void assert_persistent_equal (const persistent_snapshot_t *snapshot)
{
	assert (memcmp (&snapshot->pendingcmd, &cl.pendingcmd, sizeof snapshot->pendingcmd) == 0);
	assert (memcmp (&snapshot->cmd, &cl.cmd, sizeof snapshot->cmd) == 0);
	assert (memcmp (snapshot->movecmds, cl.movecmds, sizeof snapshot->movecmds) == 0);
	assert (snapshot->movemessages == cl.movemessages);
	assert (snapshot->ackedmovemessages == cl.ackedmovemessages);
	assert (snapshot->move_msec_sample_time == cl.move_msec_sample_time);
	assert (snapshot->move_msec_fractional_carry == cl.move_msec_fractional_carry);
	assert (snapshot->move_msec_sample_valid == cl.move_msec_sample_valid);
	assert (snapshot->net_move_msec_generated == cl.net_move_msec_generated);
	assert (snapshot->time == cl.time && snapshot->oldtime == cl.oldtime);
	assert (snapshot->mtime[0] == cl.mtime[0] && snapshot->mtime[1] == cl.mtime[1]);
	assert (memcmp (snapshot->viewangles, cl.viewangles, sizeof snapshot->viewangles) == 0);
}

static void setup (void)
{
	suppress_uncalibrated_attack = false;
	memset (&cl, 0, sizeof cl);
	memset (&cls, 0, sizeof cls);
	for (unsigned int i = 0; i < countof (tracked_keys); ++i)
		memset (tracked_keys[i], 0, sizeof *tracked_keys[i]);
	in_impulse = 0;

	cls.signon = SIGNONS;
	cl_upspeed.value = 200;
	cl_forwardspeed.value = 200;
	cl_backspeed.value = 200;
	cl_sidespeed.value = 350;
	cl_movespeedkey.value = 2;
	cl_alwaysrun.value = 0;

	cl.viewangles[PITCH] = 12.5f;
	cl.viewangles[YAW] = 123.0f;
	cl.viewangles[ROLL] = -7.0f;
	cl.time = 42.5;
	cl.oldtime = 42.25;
	cl.mtime[0] = 42.4;
	cl.mtime[1] = 42.3;
	cl.movemessages = 37;
	cl.ackedmovemessages = 31;
	cl.move_msec_sample_valid = true;
	cl.move_msec_sample_time = 1234.25;
	cl.move_msec_fractional_carry = 0.625;
	cl.net_move_msec_generated = 9876;

	cl.pendingcmd.servertime = 40.0f;
	/* Device axis plus accumulator model simultaneous mouse/joystick input. */
	cl.pendingcmd.forwardmove = 12.0f;
	cl.pendingcmd.forwardmove_accumulator = 0.25f;
	cl.pendingcmd.sidemove = -10.0f;
	cl.pendingcmd.sidemove_accumulator = 1.5f;
	cl.pendingcmd.upmove = 6.0f;
	cl.pendingcmd.upmove_accumulator = -2.0f;

	cl.cmd.sequence = 9001;
	cl.cmd.forwardmove = -77.0f;
	for (int i = 0; i < 64; ++i)
	{
		cl.movecmds[i].sequence = 1000 + i;
		cl.movecmds[i].forwardmove = i + 0.25f;
		cl.movecmds[i].buttons = (unsigned int)(i & 7);
	}

	/* Held, release-only and press/release edges exercise native key owners. */
	in_forward.state = 1;
	in_moveright.state = 3;
	in_moveleft.state = 4;
	in_up.state = 6;
	in_attack.state = 3;
	in_jump.state = 3;
	in_use.state = 4;
	in_impulse = 17;
}

static void assert_preview_value (const usercmd_t *cmd)
{
	assert (cmd->viewangles[PITCH] == 12.5f);
	assert (cmd->viewangles[YAW] == 123.0f);
	assert (cmd->viewangles[ROLL] == -7.0f);
	assert (cmd->forwardmove == 212.25f);
	assert (cmd->sidemove == 166.5f);
	assert (cmd->upmove == 54.0f);
	assert (cmd->buttons == 3);
	assert (cmd->impulse == 17);
	assert (cmd->sequence == 0);
	assert (cmd->servertime == 0 && cmd->seconds == 0 && cmd->msec == 0);
}

static usercmd_t build_final_command (void)
{
	usercmd_t cmd;

	CL_BaseMove (&cmd);
	cmd.forwardmove += cl.pendingcmd.forwardmove + cl.pendingcmd.forwardmove_accumulator;
	cmd.sidemove += cl.pendingcmd.sidemove + cl.pendingcmd.sidemove_accumulator;
	cmd.upmove += cl.pendingcmd.upmove + cl.pendingcmd.upmove_accumulator;
	cmd.sequence = cl.movemessages;
	cmd.servertime = cl.time;
	cmd.seconds = cmd.servertime - cl.pendingcmd.servertime;
	CL_FinishMove (&cmd);

	return cmd;
}

static void run_case (int preview_count, usercmd_t *final, usercmd_t *preview_result)
{
	input_snapshot_t input_before;
	persistent_snapshot_t persistent_before;
	usercmd_t first_preview = {0};

	setup ();
	capture_input (&input_before);
	capture_persistent (&persistent_before);

	for (int i = 0; i < preview_count; ++i)
	{
		usercmd_t preview;
		CL_PreviewMove (&preview);
		assert_preview_value (&preview);
		if (i == 0)
			first_preview = preview;
		else
			assert (memcmp (&first_preview, &preview, sizeof preview) == 0);
	}

	assert_input_equal (&input_before);
	assert_persistent_equal (&persistent_before);
	if (preview_count && preview_result)
		*preview_result = first_preview;

	*final = build_final_command ();
	assert (final->forwardmove == 212.25f && final->sidemove == 166.5f && final->upmove == 54.0f);
	assert (final->buttons == 3 && final->impulse == 17);
	assert (final->sequence == 37 && final->servertime == 42.5f && final->seconds == 2.5f);
	assert (final->msec == 0);

	/* Public wrappers retain their consuming behavior. */
	assert (in_forward.state == 1);
	assert (in_moveright.state == 1);
	assert (in_moveleft.state == 0);
	assert (in_up.state == 0);
	assert (in_attack.state == 1 && in_jump.state == 1);
	assert (in_impulse == 0);
}

int main (void)
{
	usercmd_t zero_preview_final;
	usercmd_t one_preview_final, one_preview;
	usercmd_t repeated_preview_final, repeated_preview;

	run_case (0, &zero_preview_final, NULL);
	run_case (1, &one_preview_final, &one_preview);
	run_case (8, &repeated_preview_final, &repeated_preview);

	assert (memcmp (&zero_preview_final, &one_preview_final, sizeof zero_preview_final) == 0);
	assert (memcmp (&zero_preview_final, &repeated_preview_final, sizeof zero_preview_final) == 0);
	assert (memcmp (&one_preview, &repeated_preview, sizeof one_preview) == 0);

	/* The shared finish boundary suppresses firing in preview and send,
	 * without swallowing the held attack edge or respawn button owner. */
	setup ();
	suppress_uncalibrated_attack = true;
	usercmd_t denied_preview = {0};
	CL_PreviewMove (&denied_preview);
	assert (!(denied_preview.buttons & 1));
	assert (in_attack.state == 3);
	usercmd_t denied_final = build_final_command ();
	assert (!(denied_final.buttons & 1));
	assert (in_attack.state == 1);
	suppress_uncalibrated_attack = false;
	usercmd_t recovered_final = build_final_command ();
	assert (recovered_final.buttons & 1);

	puts ("Client input preview: repeated previews preserve owners/history/clocks and final command bytes");
}
