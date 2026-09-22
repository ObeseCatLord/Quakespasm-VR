/* Public partial-command regression through the production host-order owners. */
#include "../Quake/cl_main.c"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

double host_frametime;
double realtime;
server_t sv;
server_static_t svs;

extern kbutton_t in_mlook, in_klook;
extern kbutton_t in_left, in_right, in_forward, in_back;
extern kbutton_t in_lookup, in_lookdown, in_moveleft, in_moveright;
extern kbutton_t in_strafe, in_speed, in_use, in_jump, in_attack;
extern kbutton_t in_up, in_down;
extern int in_impulse;

static qboolean fixture_joystick_held;
static float fixture_joystick_side;
static float fixture_mouse_side_delta;

static usercmd_t captured_send[8];
static int captured_send_count;

void V_StopPitchDrift (void)
{
	assert (!"unexpected pitch-drift side effect");
}

void IN_Move (usercmd_t *cmd)
{
	/* Match the external device boundary: joystick axes are latest-state while
	 * mouse-style movement accumulates until the next send. */
	cmd->forwardmove = 0;
	cmd->sidemove = 0;
	cmd->upmove = 0;
	if (fixture_joystick_held)
		cmd->sidemove = fixture_joystick_side;
	cmd->sidemove_accumulator += fixture_mouse_side_delta;
}

qboolean NET_CanSendMessage (struct qsocket_s *sock)
{
	(void)sock;
	assert (!"reliable transport should be bypassed by demo playback");
	return false;
}

int NET_SendMessage (struct qsocket_s *sock, sizebuf_t *data)
{
	(void)sock;
	(void)data;
	assert (!"reliable transport should be bypassed by demo playback");
	return -1;
}

int NET_SendUnreliableMessage (struct qsocket_s *sock, sizebuf_t *data)
{
	(void)sock;
	(void)data;
	assert (!"unreliable transport should be bypassed by demo playback");
	return -1;
}

qboolean NET_QSocketGetProQuakeAngleHack (const struct qsocket_s *sock)
{
	(void)sock;
	return false;
}

void Con_Printf (const char *fmt, ...)
{
	(void)fmt;
}

void Con_DPrintf (const char *fmt, ...)
{
	(void)fmt;
}

FUNC_NORETURN void Host_Error (const char *fmt, ...)
{
	(void)fmt;
	abort ();
}

FUNC_NORETURN void Sys_Error (const char *fmt, ...)
{
	(void)fmt;
	abort ();
}

void __wrap_CL_Disconnect (void)
{
	assert (!"disconnect should be unreachable in demo playback");
}

void __real_CL_SendMove (const usercmd_t *cmd);

void __wrap_CL_SendMove (const usercmd_t *cmd)
{
	assert (cmd != NULL);
	assert (captured_send_count < (int)countof(captured_send));
	captured_send[captured_send_count++] = *cmd;
	__real_CL_SendMove (cmd);
}

typedef struct
{
	usercmd_t sent[3];
	usercmd_t journal[3];
	usercmd_t held_pending_before_send;
	unsigned int forward_state_before_send;
	unsigned int attack_state_before_send;
	int impulse_before_send;
	int movemessages;
} schedule_result_t;

static void assert_close (float actual, float expected)
{
	assert (fabsf (actual - expected) < 0.0001f);
}

static void reset_input_owners (void)
{
	kbutton_t *const keys[] = {
		&in_mlook, &in_klook, &in_left, &in_right, &in_forward, &in_back,
		&in_lookup, &in_lookdown, &in_moveleft, &in_moveright,
		&in_strafe, &in_speed, &in_use, &in_jump, &in_attack, &in_up, &in_down,
	};

	for (unsigned int i = 0; i < countof(keys); ++i)
		memset (keys[i], 0, sizeof(*keys[i]));
	in_impulse = 0;
}

static void setup (void)
{
	memset (&cl, 0, sizeof(cl));
	memset (&cls, 0, sizeof(cls));
	memset (captured_send, 0, sizeof(captured_send));
	captured_send_count = 0;
	reset_input_owners ();

	cls.state = ca_connected;
	cls.signon = SIGNONS;
	cls.demoplayback = true;
	cl.time = 10.0;
	cl.oldtime = 9.95;
	cl.pendingcmd.servertime = 9.9f;
	cl.viewangles[YAW] = 20;

	host_frametime = 0.05;
	realtime = 100;

	cl_upspeed.value = 200;
	cl_forwardspeed.value = 200;
	cl_backspeed.value = 200;
	cl_sidespeed.value = 350;
	cl_movespeedkey.value = 2;
	cl_yawspeed.value = 140;
	cl_pitchspeed.value = 150;
	cl_anglespeedkey.value = 1.5;
	cl_alwaysrun.value = 0;
	cl_maxpitch.value = 90;
	cl_minpitch.value = -90;

	fixture_joystick_held = true;
	fixture_joystick_side = 64;
	fixture_mouse_side_delta = 1.25f;

	/* Press edges survive previews and are consumed only by the real final builders. */
	in_forward.state = 3;
	in_moveleft.state = 4;
	in_attack.state = 3;
	in_impulse = 17;
}

static void assert_preview_repeat (int repeat_count, float seconds, float sidemove)
{
	usercmd_t first = {0};
	usercmd_t pending_before = cl.pendingcmd;
	unsigned int forward_before = in_forward.state;
	unsigned int moveleft_before = in_moveleft.state;
	unsigned int attack_before = in_attack.state;
	int impulse_before = in_impulse;

	for (int i = 0; i < repeat_count; ++i)
	{
		usercmd_t preview;
		CL_PrepareReplayPreview (&preview, false);
		assert_close (preview.seconds, seconds);
		assert_close (preview.sidemove, sidemove);
		if (i == 0)
			first = preview;
		else
			assert (memcmp (&first, &preview, sizeof(preview)) == 0);
	}

	assert (memcmp (&pending_before, &cl.pendingcmd, sizeof(pending_before)) == 0);
	assert (forward_before == in_forward.state);
	assert (moveleft_before == in_moveleft.state);
	assert (attack_before == in_attack.state);
	assert (impulse_before == in_impulse);
}

static void advance_read_time (void)
{
	/* These are the time-owner lines at the start of CL_ReadFromServer.  Calling
	 * the whole reader would retain relink/temp-entity/render systems unrelated
	 * to this bounded input/preview regression. */
	cl.oldtime = cl.time;
	cl.time += host_frametime;
}

static void run_schedule (int extra_preview_count, schedule_result_t *result)
{
	usercmd_t preview;

	setup ();

	/* Frame 1: accumulate held joystick and mouse delta, then send. */
	CL_AccumulateCmd ();
	assert_close (cl.pendingcmd.sidemove, 64);
	assert_close (cl.pendingcmd.sidemove_accumulator, 1.25f);
	assert_close (cl.pendingcmd.seconds, 0.1f);
	assert_preview_repeat (extra_preview_count, 0.1f, 65.25f);
	CL_SendCmd ();
	assert (captured_send_count == 1 && captured_send[0].sequence == 0);
	assert_close (captured_send[0].sidemove, 65.25f);
	assert (captured_send[0].buttons == 1 && captured_send[0].impulse == 17);
	assert (cl.movemessages == 1);
	assert (cl.pendingcmd.sidemove == 0 && cl.pendingcmd.sidemove_accumulator == 0);
	assert (cl.pendingcmd.seconds == 0 && cl.pendingcmd.servertime == 10.0f);
	assert (in_forward.state == 1 && in_moveleft.state == 0 && in_attack.state == 1);
	assert (in_impulse == 0);

	/* Read-time advancement happens after the send.  The cleared pending command
	 * must remain zero-duration even though cl.time is now newer than servertime. */
	advance_read_time ();
	assert (cl.time > cl.pendingcmd.servertime);
	CL_PrepareReplayPreview (&preview, false);
	assert (preview.seconds == 0);
	assert_close (preview.sidemove, 0);
	assert_preview_repeat (extra_preview_count, 0, 0);

	/* Frame 2: no send.  The held joystick is withheld in pendingcmd.  Another
	 * read-time advance must not replace the duration sampled by accumulation. */
	fixture_mouse_side_delta = 2.0f;
	CL_AccumulateCmd ();
	assert_close (cl.pendingcmd.sidemove, 64);
	assert_close (cl.pendingcmd.sidemove_accumulator, 2);
	assert_close (cl.pendingcmd.seconds, 0.05f);
	advance_read_time ();
	assert_close ((float)(cl.time - cl.pendingcmd.servertime), 0.1f);
	CL_PrepareReplayPreview (&preview, false);
	assert_close (preview.seconds, 0.05f);
	assert_close (preview.sidemove, 66);
	assert_preview_repeat (extra_preview_count, 0.05f, 66);

	/* Frame 3: the next accumulation replaces the joystick axis and extends only
	 * the mouse accumulator; the send consumes exactly that pending state. */
	fixture_mouse_side_delta = 3.0f;
	CL_AccumulateCmd ();
	assert_close (cl.pendingcmd.sidemove, 64);
	assert_close (cl.pendingcmd.sidemove_accumulator, 5);
	assert_close (cl.pendingcmd.seconds, 0.1f);
	result->held_pending_before_send = cl.pendingcmd;
	result->forward_state_before_send = in_forward.state;
	result->attack_state_before_send = in_attack.state;
	result->impulse_before_send = in_impulse;
	assert_preview_repeat (extra_preview_count, 0.1f, 69);
	CL_SendCmd ();
	assert (captured_send_count == 2 && captured_send[1].sequence == 1);
	assert_close (captured_send[1].sidemove, 69);
	assert_close (captured_send[1].forwardmove, 200);
	assert (cl.movemessages == 2);
	assert (cl.pendingcmd.sidemove == 0 && cl.pendingcmd.sidemove_accumulator == 0);
	advance_read_time ();
	CL_PrepareReplayPreview (&preview, false);
	assert (preview.seconds == 0 && preview.sidemove == 0);
	assert_preview_repeat (extra_preview_count, 0, 0);

	/* Frame 4: no send with the joystick released.  Positive pending duration is
	 * retained, but the old held axis must not survive the external-device refresh. */
	fixture_joystick_held = false;
	fixture_mouse_side_delta = 0;
	CL_AccumulateCmd ();
	assert (cl.pendingcmd.sidemove == 0 && cl.pendingcmd.sidemove_accumulator == 0);
	assert_close (cl.pendingcmd.seconds, 0.05f);
	advance_read_time ();
	CL_PrepareReplayPreview (&preview, false);
	assert_close (preview.seconds, 0.05f);
	assert (preview.sidemove == 0);
	assert_preview_repeat (extra_preview_count, 0.05f, 0);

	/* Frame 5: released joystick remains absent from the next final command. */
	fixture_mouse_side_delta = 4.0f;
	CL_AccumulateCmd ();
	assert (cl.pendingcmd.sidemove == 0);
	assert_close (cl.pendingcmd.sidemove_accumulator, 4);
	assert_close (cl.pendingcmd.seconds, 0.1f);
	assert_preview_repeat (extra_preview_count, 0.1f, 4);
	CL_SendCmd ();
	assert (captured_send_count == 3 && captured_send[2].sequence == 2);
	assert_close (captured_send[2].sidemove, 4);
	assert_close (captured_send[2].forwardmove, 200);
	assert (cl.movemessages == 3);

	memcpy (result->sent, captured_send, sizeof(result->sent));
	for (int i = 0; i < 3; ++i)
		result->journal[i] = cl.movecmds[i];
	result->movemessages = cl.movemessages;
}

int main (void)
{
	schedule_result_t no_extra_preview = {0};
	schedule_result_t repeated_preview = {0};

	run_schedule (0, &no_extra_preview);
	run_schedule (8, &repeated_preview);

	assert (memcmp (no_extra_preview.sent, repeated_preview.sent,
		sizeof(no_extra_preview.sent)) == 0);
	assert (memcmp (no_extra_preview.journal, repeated_preview.journal,
		sizeof(no_extra_preview.journal)) == 0);
	assert (memcmp (&no_extra_preview.held_pending_before_send,
		&repeated_preview.held_pending_before_send,
		sizeof(no_extra_preview.held_pending_before_send)) == 0);
	assert (no_extra_preview.forward_state_before_send == repeated_preview.forward_state_before_send);
	assert (no_extra_preview.attack_state_before_send == repeated_preview.attack_state_before_send);
	assert (no_extra_preview.impulse_before_send == repeated_preview.impulse_before_send);
	assert (no_extra_preview.movemessages == repeated_preview.movemessages);

	puts ("public replay preview: host-order pending duration/input survives no-send frames and clears on send");
	return 0;
}
