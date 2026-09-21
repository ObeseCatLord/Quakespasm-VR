#include "../Quake/quakedef.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

sizebuf_t net_message;

void Host_Error (const char *error, ...)
{
	(void)error;
	assert (!"unexpected sizebuf overflow");
}

void Sys_Error (const char *error, ...)
{
	(void)error;
	assert (!"unexpected sizebuf overflow");
}

void Con_Printf (const char *error, ...)
{
	(void)error;
}

static void near_value (float actual, float expected)
{
	assert (fabsf (actual - expected) < 0.01f);
}

static void begin_read (const byte *data, int size)
{
	net_message.data = (byte *)data;
	net_message.cursize = size;
	net_message.maxsize = size;
	net_message.allowoverflow = false;
	net_message.overflowed = false;
	MSG_BeginReading ();
}

static void begin_write (sizebuf_t *buf, byte *data, int size)
{
	buf->data = data;
	buf->cursize = 0;
	buf->maxsize = size;
	buf->allowoverflow = false;
	buf->overflowed = false;
}

static void assert_packet (const sizebuf_t *buf, const byte *expected, int expected_size)
{
	assert (buf->cursize == expected_size);
	assert (memcmp (buf->data, expected, expected_size) == 0);
}

static void assert_nonfinite_rejected (const byte *finite, int size, int float_offset, unsigned int protocolflags)
{
	static const byte nonfinite[][4] = {{0x00, 0x00, 0xc0, 0x7f}, {0x00, 0x00, 0x80, 0x7f}, {0x00, 0x00, 0x80, 0xff}};
	byte			  malformed[128];
	usercmd_t		  decoded;
	int				  variant;

	assert (size <= (int)sizeof (malformed));
	for (variant = 0; variant < (int)(sizeof (nonfinite) / sizeof (nonfinite[0])); variant++)
	{
		memcpy (malformed, finite, size);
		memcpy (malformed + float_offset, nonfinite[variant], 4);
		begin_read (malformed, size);
		assert (!SV_ReadPrivateUsercmd (&decoded, 13, protocolflags, 0));
		assert (msg_badread);
	}
}

static void assert_base_command (const usercmd_t *cmd, unsigned int sequence, float servertime, unsigned char msec)
{
	assert (cmd->sequence == sequence);
	near_value (cmd->servertime, servertime);
	assert (cmd->msec == msec);
	assert (cmd->vr_contact_received == 0);
}

static void set_full_command (usercmd_t *cmd, qboolean trusted)
{
	int hand;
	int axis;

	memset (cmd, 0, sizeof (*cmd));
	cmd->servertime = 12.5f;
	cmd->msec = 17;
	cmd->viewangles[0] = -10.0f;
	cmd->viewangles[1] = 90.0f;
	cmd->viewangles[2] = 45.0f;
	cmd->forwardmove = 300;
	cmd->sidemove = -200;
	cmd->upmove = 50;
	cmd->buttons = 0xa5;
	cmd->impulse = 7;
	cmd->weapon = 0x10203040;
	cmd->cursor_screen[0] = -0.5f;
	cmd->cursor_screen[1] = 0.75f;
	cmd->cursor_start[0] = 1.0f;
	cmd->cursor_start[1] = 2.0f;
	cmd->cursor_start[2] = 3.0f;
	cmd->cursor_impact[0] = 4.0f;
	cmd->cursor_impact[1] = 5.0f;
	cmd->cursor_impact[2] = 6.0f;
	cmd->cursor_entitynumber = 0x123456;
	cmd->vr_active = true;
	cmd->vr_handpos_relative = true;
	cmd->vr_akimbo_active = true;
	cmd->vr_akimbo_berserk = true;
	cmd->vr_contact.flags = VR_WEAPON_CONTACT_LEFT_VALID | VR_WEAPON_CONTACT_RIGHT_VALID | VR_WEAPON_CONTACT_IMMERSIVE_MELEE;
	cmd->vr_contact.modelindex = 65535;
	cmd->vr_contact.weapon = 8.0f;

	for (axis = 0; axis < 3; ++axis)
	{
		cmd->vr_handpos[axis] = 10.0f + axis;
		cmd->vr_handrot[axis] = 20.0f + axis;
		cmd->vr_roomscalemove[axis] = 0.25f + axis;
		cmd->cursor_start[axis] = 1.0f + axis;
		cmd->cursor_impact[axis] = 4.0f + axis;
		cmd->vr_gorilla.head[axis] = 3.0f + axis;
		cmd->vr_gorilla_motion.displacement[axis] = trusted ? 0.5f + axis : 0;
		cmd->vr_gorilla_motion.impulse[axis] = trusted ? 10.0f + axis : 0;
	}
	for (hand = 0; hand < 2; ++hand)
	{
		for (axis = 0; axis < 3; ++axis)
		{
			cmd->vr_akimbo_muzzle[hand][axis] = 30.0f + hand * 3 + axis;
			cmd->vr_akimbo_angles[hand][axis] = 40.0f + hand * 3 + axis;
			cmd->vr_contact.grip[hand][axis] = 50.0f + hand * 3 + axis;
			cmd->vr_contact.base[hand][axis] = 60.0f + hand * 3 + axis;
			cmd->vr_contact.tip[hand][axis] = 70.0f + hand * 3 + axis;
			cmd->vr_gorilla.hand[hand][axis] = 10.0f + hand * 4 + axis;
			cmd->vr_gorilla.velocity[hand][axis] = 20.0f + hand * 4 + axis;
		}
		cmd->vr_contact.speed[hand] = 80.0f + hand;
	}
	cmd->vr_gorilla.flags = VR_GORILLA_HANDS;
	cmd->vr_gorilla_motion.flags = trusted ? VR_GORILLA_MOTION_ACTIVE | VR_GORILLA_MOTION_BRACED : 0;
	cmd->vr_gorilla_motion.generation = 0x55667788u;
	cmd->vr_gorilla_motion.contact[0] = 31;
	cmd->vr_gorilla_motion.contact[1] = MAX_EDICTS - 1;
	cmd->vr_gorilla_motion.contact_model[0] = QSVR_MODEL_LIMIT - 1;
	cmd->vr_gorilla_motion.contact_model[1] = 1;
}

static void assert_full_command (const usercmd_t *actual, const usercmd_t *expected, qboolean trusted)
{
	int hand;
	int axis;

	assert_base_command (actual, 77, expected->servertime, expected->msec);
	near_value (actual->viewangles[0], expected->viewangles[0]);
	near_value (actual->viewangles[1], expected->viewangles[1]);
	near_value (actual->viewangles[2], expected->viewangles[2]);
	near_value (actual->forwardmove, expected->forwardmove);
	near_value (actual->sidemove, expected->sidemove);
	near_value (actual->upmove, expected->upmove);
	assert (actual->buttons == expected->buttons);
	assert (actual->impulse == expected->impulse);
	assert (actual->weapon == expected->weapon);
	near_value (actual->cursor_screen[0], expected->cursor_screen[0]);
	near_value (actual->cursor_screen[1], expected->cursor_screen[1]);
	assert (actual->cursor_entitynumber == expected->cursor_entitynumber);
	assert (actual->vr_active && actual->vr_handpos_relative);
	assert (actual->vr_akimbo_active && actual->vr_akimbo_berserk);
	assert (actual->vr_contact.flags == expected->vr_contact.flags);
	assert (actual->vr_contact.modelindex == 65535);
	near_value (actual->vr_contact.weapon, expected->vr_contact.weapon);
	for (axis = 0; axis < 3; ++axis)
	{
		near_value (actual->vr_handpos[axis], expected->vr_handpos[axis]);
		near_value (actual->vr_handrot[axis], expected->vr_handrot[axis]);
		near_value (actual->vr_roomscalemove[axis], expected->vr_roomscalemove[axis]);
		near_value (actual->cursor_start[axis], expected->cursor_start[axis]);
		near_value (actual->cursor_impact[axis], expected->cursor_impact[axis]);
	}
	for (hand = 0; hand < 2; ++hand)
	{
		for (axis = 0; axis < 3; ++axis)
		{
			near_value (actual->vr_akimbo_muzzle[hand][axis], expected->vr_akimbo_muzzle[hand][axis]);
			near_value (actual->vr_akimbo_angles[hand][axis], expected->vr_akimbo_angles[hand][axis]);
			near_value (actual->vr_contact.grip[hand][axis], expected->vr_contact.grip[hand][axis]);
			near_value (actual->vr_contact.base[hand][axis], expected->vr_contact.base[hand][axis]);
			near_value (actual->vr_contact.tip[hand][axis], expected->vr_contact.tip[hand][axis]);
		}
		near_value (actual->vr_contact.speed[hand], expected->vr_contact.speed[hand]);
	}
	if (trusted)
	{
		assert (actual->vr_gorilla.flags == 0);
		assert (actual->vr_gorilla_motion.flags == expected->vr_gorilla_motion.flags);
		assert (actual->vr_gorilla_motion.generation == expected->vr_gorilla_motion.generation);
		for (hand = 0; hand < 2; ++hand)
		{
			assert (actual->vr_gorilla_motion.contact[hand] == expected->vr_gorilla_motion.contact[hand]);
			assert (actual->vr_gorilla_motion.contact_model[hand] == expected->vr_gorilla_motion.contact_model[hand]);
		}
		for (axis = 0; axis < 3; ++axis)
		{
			near_value (actual->vr_gorilla_motion.displacement[axis], expected->vr_gorilla_motion.displacement[axis]);
			near_value (actual->vr_gorilla_motion.impulse[axis], expected->vr_gorilla_motion.impulse[axis]);
		}
	}
	else
	{
		assert (actual->vr_gorilla.flags == expected->vr_gorilla.flags);
		for (axis = 0; axis < 3; ++axis)
			near_value (actual->vr_gorilla.head[axis], expected->vr_gorilla.head[axis]);
		for (hand = 0; hand < 2; ++hand)
			for (axis = 0; axis < 3; ++axis)
			{
				near_value (actual->vr_gorilla.hand[hand][axis], expected->vr_gorilla.hand[hand][axis]);
				near_value (actual->vr_gorilla.velocity[hand][axis], expected->vr_gorilla.velocity[hand][axis]);
			}
	}
}

static void test_default_angle_golden (void)
{
	static const byte expected[] = {0x00, 0x00, 0xc0, 0x3f, 0x10, 0x00, 0x00, 0x00, 0x40, 0x00, 0x80, 0x01, 0x00, 0xfe, 0xff, 0x2c, 0x01, 0xaa, 0x7f, 0x00};
	byte			  data[128];
	sizebuf_t		  buf;
	usercmd_t		  command = {0};
	usercmd_t		  decoded;

	command.servertime = 1.5f;
	command.msec = 16;
	command.viewangles[1] = 90;
	command.viewangles[2] = 180;
	command.forwardmove = 1;
	command.sidemove = -2;
	command.upmove = 300;
	command.buttons = 0xaa;
	command.impulse = 0x7f;
	begin_write (&buf, data, sizeof (data));
	CL_WritePrivateUsercmd (&buf, &command, 0, 0);
	assert_packet (&buf, expected, sizeof (expected));
	begin_read (expected, sizeof (expected));
	assert (SV_ReadPrivateUsercmd (&decoded, 7, 0, 0));
	assert_base_command (&decoded, 7, 1.5f, 16);
	near_value (decoded.viewangles[1], 90);
	near_value (decoded.viewangles[2], -180);
	near_value (decoded.forwardmove, 1);
	near_value (decoded.sidemove, -2);
	near_value (decoded.upmove, 300);
	assert (decoded.buttons == 0xaa && decoded.impulse == 0x7f);
	assert (msg_readcount == (int)sizeof (expected));
	assert_nonfinite_rejected (expected, sizeof (expected), 0, 0);
}

static void test_float_angle_golden (void)
{
	static const byte expected[] = {0x00, 0x00, 0xc0, 0x3f, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x3f,
									0x00, 0x00, 0x00, 0xc0, 0x01, 0x00, 0xfe, 0xff, 0x2c, 0x01, 0xaa, 0x7f, 0x00};
	byte			  data[128];
	sizebuf_t		  buf;
	usercmd_t		  command = {0};
	usercmd_t		  decoded;

	command.servertime = 1.5f;
	command.msec = 16;
	command.viewangles[1] = 1;
	command.viewangles[2] = -2;
	command.forwardmove = 1;
	command.sidemove = -2;
	command.upmove = 300;
	command.buttons = 0xaa;
	command.impulse = 0x7f;
	begin_write (&buf, data, sizeof (data));
	CL_WritePrivateUsercmd (&buf, &command, PRFL_FLOATANGLE, 0);
	assert_packet (&buf, expected, sizeof (expected));
	begin_read (expected, sizeof (expected));
	assert (SV_ReadPrivateUsercmd (&decoded, 8, PRFL_FLOATANGLE, 0));
	assert_base_command (&decoded, 8, 1.5f, 16);
	near_value (decoded.viewangles[1], 1);
	near_value (decoded.viewangles[2], -2);
	assert (msg_readcount == (int)sizeof (expected));
	for (int axis = 0; axis < 3; ++axis)
		assert_nonfinite_rejected (expected, sizeof (expected), 5 + axis * 4, PRFL_FLOATANGLE);
}

static void test_extended_entity_golden (void)
{
	static const byte expected[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
									0x04, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
									0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0x92, 0x56};
	byte			  data[128];
	sizebuf_t		  buf;
	usercmd_t		  command = {0};
	usercmd_t		  decoded;

	command.msec = 1;
	command.weapon = 0x01020304;
	command.cursor_entitynumber = 0x123456;
	begin_write (&buf, data, sizeof (data));
	CL_WritePrivateUsercmd (&buf, &command, 0, 0);
	assert_packet (&buf, expected, sizeof (expected));
	begin_read (expected, sizeof (expected));
	assert (SV_ReadPrivateUsercmd (&decoded, 9, 0, 0));
	assert (decoded.weapon == 0x01020304);
	assert (decoded.cursor_entitynumber == 0x123456);
	assert (msg_readcount == (int)sizeof (expected));
	for (int component = 0; component < 6; ++component)
		assert_nonfinite_rejected (expected, sizeof (expected), 28 + component * 4, 0);
}

static void test_full_payload_roundtrips (void)
{
	byte	  data[1024];
	sizebuf_t buf;
	usercmd_t command;
	usercmd_t decoded;

	set_full_command (&command, false);
	begin_write (&buf, data, sizeof (data));
	CL_WritePrivateUsercmd (&buf, &command, 0, QSVR_MOVE_CAP_GORILLA_RAW);
	begin_read (data, buf.cursize);
	assert (SV_ReadPrivateUsercmd (&decoded, 77, 0, 0));
	assert (msg_readcount == buf.cursize);
	assert_full_command (&decoded, &command, false);

	set_full_command (&command, true);
	begin_write (&buf, data, sizeof (data));
	CL_WritePrivateUsercmd (&buf, &command, 0, QSVR_MOVE_CAP_GORILLA_TRUSTED);
	begin_read (data, buf.cursize);
	assert (SV_ReadPrivateUsercmd (&decoded, 77, 0, QSVR_MOVE_CAP_GORILLA_TRUSTED));
	assert (msg_readcount == buf.cursize);
	assert_full_command (&decoded, &command, true);
}

static void test_trusted_capability_and_model_limits (void)
{
	byte	  data[1024];
	sizebuf_t buf;
	usercmd_t command;
	usercmd_t decoded;

	set_full_command (&command, true);
	begin_write (&buf, data, sizeof (data));
	CL_WritePrivateUsercmd (&buf, &command, 0, QSVR_MOVE_CAP_GORILLA_TRUSTED);
	begin_read (data, buf.cursize);
	assert (!SV_ReadPrivateUsercmd (&decoded, 10, 0, 0));
	assert (msg_badread);
	begin_read (data, buf.cursize);
	assert (SV_ReadPrivateUsercmd (&decoded, 10, 0, QSVR_MOVE_CAP_GORILLA_TRUSTED));
	assert (msg_readcount == buf.cursize);

	command.vr_gorilla_motion.contact_model[0] = QSVR_MODEL_LIMIT;
	begin_write (&buf, data, sizeof (data));
	CL_WritePrivateUsercmd (&buf, &command, 0, QSVR_MOVE_CAP_GORILLA_TRUSTED);
	begin_read (data, buf.cursize);
	assert (!SV_ReadPrivateUsercmd (&decoded, 11, 0, QSVR_MOVE_CAP_GORILLA_TRUSTED));
	assert (msg_badread);
}

static void assert_all_prefixes_rejected (const usercmd_t *command, unsigned int protocolflags, unsigned int capabilities)
{
	byte	  data[1024];
	sizebuf_t buf;
	usercmd_t decoded;
	int		  size;

	begin_write (&buf, data, sizeof (data));
	CL_WritePrivateUsercmd (&buf, command, protocolflags, capabilities);
	for (size = 0; size < buf.cursize; ++size)
	{
		byte *prefix = malloc (size ? (size_t)size : 1);

		assert (prefix != NULL);
		if (size)
			memcpy (prefix, data, size);
		begin_read (prefix, size);
		assert (!SV_ReadPrivateUsercmd (&decoded, 12, protocolflags, capabilities));
		assert (msg_badread);
		free (prefix);
	}
}

static void test_all_prefix_truncations (void)
{
	usercmd_t command;

	set_full_command (&command, false);
	assert_all_prefixes_rejected (&command, 0, QSVR_MOVE_CAP_GORILLA_RAW);
	assert_all_prefixes_rejected (&command, PRFL_FLOATANGLE, QSVR_MOVE_CAP_GORILLA_RAW);
	set_full_command (&command, true);
	assert_all_prefixes_rejected (&command, 0, QSVR_MOVE_CAP_GORILLA_TRUSTED);
}

int main (void)
{
	test_default_angle_golden ();
	test_float_angle_golden ();
	test_extended_entity_golden ();
	test_full_payload_roundtrips ();
	test_trusted_capability_and_model_limits ();
	test_all_prefix_truncations ();
	puts ("Private usercmd codec golden bytes, payloads, capabilities, bounds, and truncation checks passed");
	return 0;
}
