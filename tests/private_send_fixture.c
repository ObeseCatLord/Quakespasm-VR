/* Real sender/MSG codecs; only the network socket and diagnostics are captured.
 * This checks transport construction, not a live connection or player motion. */
#include "../Quake/cl_input.c"
#include <assert.h>

client_state_t	cl;
client_static_t cls;
server_t		sv;
server_static_t svs;
sizebuf_t		net_message;
double			realtime, host_frametime;
static byte		packet[DATAGRAM_MTU];
static int		packet_size, packet_count, disconnects, send_result;
void			Host_Error (const char *fmt, ...)
{
	abort ();
}
void Sys_Error (const char *fmt, ...)
{
	abort ();
}
void Con_Printf (const char *fmt, ...) {}
void CL_Disconnect (void)
{
	disconnects++;
}
qboolean NET_QSocketGetProQuakeAngleHack (const struct qsocket_s *sock)
{
	return false;
}
int NET_SendUnreliableMessage (struct qsocket_s *sock, sizebuf_t *buf)
{
	assert (buf->cursize <= sizeof packet);
	memcpy (packet, buf->data, buf->cursize);
	packet_size = buf->cursize;
	packet_count++;
	return send_result;
}
static void setup (void)
{
	memset (&cl, 0, sizeof cl);
	memset (&cls, 0, sizeof cls);
	memset (&sv, 0, sizeof sv);
	memset (&svs, 0, sizeof svs);
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.protocol = PROTOCOL_RMQ;
	cl.protocol_pext2 = QSVR_PEXT2_REQUIRED;
	cls.netcon = (struct qsocket_s *)(uintptr_t)1;
	realtime = 1;
	host_frametime = .01;
	packet_count = packet_size = disconnects = send_result = 0;
}
static void begin_packet (void)
{
	net_message.data = packet;
	net_message.cursize = packet_size;
	MSG_BeginReading ();
}
static void test_redundancy (void)
{
	setup ();
	for (int seq = 0; seq < 6; ++seq)
	{
		usercmd_t cmd = {0};
		cmd.servertime = 100 + seq; // diagnostic clock does not set duration
		cmd.viewangles[1] = 90;
		cmd.forwardmove = 200;
		cmd.buttons = seq == 2 ? 1 : 0;
		cmd.impulse = seq == 3 ? 7 : 0;
		CL_SendMove (&cmd);
		assert (cl.movemessages == seq + 1 && cl.cmd.msec >= 9 && cl.cmd.msec <= 11);
		if (seq < 2)
		{
			assert (packet_count == 0);
			assert (cl.movecmds[seq].seconds == 0);
		}
		else
		{
			begin_packet ();
			for (int previous = q_max (2, seq - 2); previous <= seq; ++previous)
			{
				usercmd_t decoded;
				assert (MSG_ReadByte () == clc_move);
				assert ((unsigned short)MSG_ReadShort () == previous);
				assert (SV_ReadPrivateUsercmd (&decoded, previous, 0, 0));
				assert (decoded.buttons == (previous == 2 ? 1u : 0u));
				assert (decoded.impulse == (previous == 3 ? 7 : 0));
				assert (decoded.servertime == 100 + previous && decoded.forwardmove == 200);
			}
			assert (!msg_badread && msg_readcount == packet_size);
		}
		realtime += .01;
	}
	assert (packet_count == 4 && cl.net_move_packets_sent == 4 && cl.net_move_cmds_sent == 9);
}
static void test_clock (void)
{
	setup ();
	host_frametime = 1.0 / 72;
	int sum = 0;
	for (int i = 0; i < 72; ++i)
	{
		sum += CL_SampleMoveMsec ();
		realtime += 1.0 / 72;
	}
	assert (sum >= 999 && sum <= 1000);
	realtime += 100000000;
	assert (CL_SampleMoveMsec () == 125 && cl.move_msec_fractional_carry == 0);
	realtime -= 1;
	assert (CL_SampleMoveMsec () == 1);
}
static void test_ack_queue (void)
{
	setup ();
	for (int i = 0; i < CL_ACKFRAME_HISTORY; ++i)
		cl.ackframes[i] = i + 100;
	cl.ackframes_count = CL_ACKFRAME_HISTORY;
	byte	  small[11];
	sizebuf_t buf = {0};
	buf.data = small;
	buf.maxsize = sizeof small;
	CL_WriteAckFrames (&buf);
	assert (buf.cursize == 10 && cl.ackframes_count == CL_ACKFRAME_HISTORY - 2);
	assert (cl.ackframes[0] == 102 && cl.ackframes[cl.ackframes_count - 1] == 227);
	assert (small[0] == clcdp_ackframe && small[1] == 100 && small[5] == clcdp_ackframe && small[6] == 101);
	CL_SendMove (NULL);
	assert (cl.ackframes_count == 0 && cl.net_snapshot_acks_sent == CL_ACKFRAME_HISTORY);
	begin_packet ();
	for (int i = 102; i < 228; ++i)
	{
		assert (MSG_ReadByte () == clcdp_ackframe);
		assert (MSG_ReadLong () == i);
	}
	assert (!msg_badread && msg_readcount == packet_size);
}
static void test_full_bundle_and_wrap (void)
{
	setup ();
	cl.movemessages = 65534;
	cl.vr_gorilla_supported = cl.vr_gorilla_allowed = true;
	usercmd_t cmd = {0};
	cmd.vr_active = cmd.vr_handpos_relative = cmd.vr_akimbo_active = true;
	cmd.weapon = 1;
	cmd.vr_contact.flags = 3;
	cmd.vr_gorilla.flags = VR_GORILLA_HANDS;
	for (int i = 0; i < 3; ++i)
	{
		if (i == 2)
		{
			cl.ackframes_count = CL_ACKFRAME_HISTORY;
			for (int ack = 0; ack < CL_ACKFRAME_HISTORY; ++ack)
				cl.ackframes[ack] = 1000 + ack;
		}
		CL_SendMove (&cmd);
		realtime += .01;
	}
	assert (packet_size <= DATAGRAM_MTU && cl.ackframes_count > 0);
	begin_packet ();
	for (int sequence = 65534; sequence <= 65536; ++sequence)
	{
		usercmd_t decoded;
		assert (MSG_ReadByte () == clc_move);
		assert ((unsigned short)MSG_ReadShort () == (sequence & 0xffff));
		assert (SV_ReadPrivateUsercmd (&decoded, sequence, 0, 0));
		assert (decoded.vr_active && decoded.vr_akimbo_active && decoded.vr_gorilla.flags == VR_GORILLA_HANDS);
	}
	int acknowledged = 0;
	while (msg_readcount < packet_size)
	{
		assert (MSG_ReadByte () == clcdp_ackframe);
		assert (MSG_ReadLong () == 1000 + acknowledged++);
	}
	assert (!msg_badread && acknowledged + cl.ackframes_count == CL_ACKFRAME_HISTORY);
	assert (cl.ackframes[0] == 1000 + acknowledged);
	CL_FlushAckFrames ();
	assert (!cl.ackframes_count);
}

static void test_public_and_demo (void)
{
	setup ();
	usercmd_t cmd = {0};
	cmd.servertime = 1;
	cl.movemessages = 2;
	cl.protocol_qsvr = 0; // the colliding FTE mask must not select private layout
	CL_SendMove (&cmd);
	assert (packet_count == 1 && packet_size == 21 && !cl.move_msec_sample_valid);
	setup ();
	cls.demoplayback = true;
	cl.ackframes[0] = 42;
	cl.ackframes_count = 1;
	CL_SendMove (&cmd);
	assert (!packet_count && !cl.movemessages && cl.ackframes_count == 1);
	cls.demoplayback = false;
	send_result = -1;
	CL_FlushAckFrames ();
	assert (disconnects == 1);
}
int main (void)
{
	test_redundancy ();
	test_clock ();
	test_ack_queue ();
	test_full_bundle_and_wrap ();
	test_public_and_demo ();
	puts ("Private sender: redundant commands, duration, ACK retention, public framing and demo/error checks passed");
}
