/* Production private move-ACK parser + production MSG readers; raw fixture bytes. */
#include "../Quake/cl_parse.c"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

client_state_t cl;
client_static_t cls;
sizebuf_t net_message;
cvar_t cl_shownet;
vec3_t v_punchangles[2];
double v_punchangles_times[2];

/* Fixture-only network and presentation boundaries. The parser and MSG readers are production code. */
struct qsocket_s { int unused; };
static struct qsocket_s socket_stub;
static int smoothing_resets;
static int resume_resets;
static int flushes;
static qboolean parse_ack_accepted;
static int parsed_source_epoch;
int NET_QSocketGetSequenceIn (const struct qsocket_s *sock) { return 77; }
void CL_ResetPredictionSmoothing (void) { smoothing_resets++; }
void CL_PrivateMoveResumeObserved (void) { resume_resets++; }
void CL_FlushAckFrames (void) { flushes++; }
void Con_DPrintf (const char *fmt, ...) {}
void Con_Printf (const char *fmt, ...) {}
void Con_SafePrintf (const char *fmt, ...) {}

static void put_byte (byte *bytes, int *length, unsigned value)
{
	bytes[(*length)++] = (byte)value;
}

static void put_short (byte *bytes, int *length, unsigned value)
{
	put_byte (bytes, length, value);
	put_byte (bytes, length, value >> 8);
}

static void put_long (byte *bytes, int *length, unsigned int value)
{
	put_short (bytes, length, value);
	put_short (bytes, length, value >> 16);
}

static void put_float (byte *bytes, int *length, float value)
{
	unsigned int bits;
	memcpy (&bits, &value, sizeof (bits));
	put_long (bytes, length, bits);
}

static int moveack (byte *bytes, unsigned ack, unsigned flags, unsigned authority,
	unsigned mode_epoch, unsigned discontinuity_epoch, unsigned reason,
	qboolean trusted, qboolean gorilla, unsigned int model)
{
	int length = 0;
	int hand, axis;

	put_short (bytes, &length, ack);
	put_byte (bytes, &length, flags);
	put_byte (bytes, &length, authority);
	put_short (bytes, &length, mode_epoch);
	put_short (bytes, &length, discontinuity_epoch);
	put_byte (bytes, &length, reason);
	if (trusted)
		put_long (bytes, &length, 0x12345678u);
	if (!gorilla)
		return length;

	put_long (bytes, &length, 19);
	put_byte (bytes, &length, 1);
	put_byte (bytes, &length, 1);
	put_byte (bytes, &length, 2);
	for (hand = 0; hand < 2; hand++)
		for (axis = 0; axis < 3; axis++)
			put_float (bytes, &length, (float)(hand * 10 + axis));
	for (hand = 0; hand < 2; hand++)
		for (axis = 0; axis < 3; axis++)
			put_float (bytes, &length, (float)(hand + axis));
	for (axis = 0; axis < 3; axis++)
		put_float (bytes, &length, (float)axis);
	for (axis = 0; axis < 3; axis++)
		put_float (bytes, &length, (float)(20 + axis));
	put_long (bytes, &length, 0);
	put_long (bytes, &length, 42);
	put_long (bytes, &length, 0);
	put_long (bytes, &length, model);
	return length;
}

static void reset_client (void)
{
	memset (&cl, 0, sizeof (cl));
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.ackedmovemessages = -1;
	cl.vr_gorilla_state_sequence = -1;
	smoothing_resets = 0;
	resume_resets = 0;
	flushes = 0;
	parse_ack_accepted = false;
	parsed_source_epoch = -1;
}

static qboolean parse (const byte *bytes, int length)
{
	net_message.data = (byte *)bytes;
	net_message.cursize = length;
	MSG_BeginReading ();
	return CL_ParseMoveAckPayload (&parse_ack_accepted, &parsed_source_epoch);
}

static void test_pending_epoch_after_lost_completion (void)
{
	byte packet[128];
	const int first_sequences[] = {40000, 65636, 65638};
	for (unsigned index = 0; index < countof (first_sequences); ++index)
	{
		const int first = first_sequences[index];
		int length;
		float commandframe = 100;
		reset_client ();
		cl.movemessages = first + 4;
		cl.ackedmovemessages = 100;
		cl.qcvm.extglobals.servercommandframe = &commandframe;
		cl.move_ack_selected_owner = true;
		cl.move_ack_discontinuity_epoch = 4;
		cl.move_resume_marker_epoch_valid = true;
		cl.move_resume_marker_epoch_sent = 4;
		cl.move_resume_marker_first_sequence = first;
		/* The server completed first, but no completion reply arrived before
		 * it published a second recovery generation. */
		length = moveack (packet, first,
			MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 5,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		assert (parse (packet, length) && parse_ack_accepted &&
			cl.move_ack_resume_pending && cl.move_ack_discontinuity_epoch == 5 &&
			cl.ackedmovemessages == 100 && commandframe == 100 && !cl.net_move_acks &&
			resume_resets == 1 && !cl.move_resume_marker_epoch_valid);
		assert (parse (packet, length) && resume_resets == 1 && cl.ackedmovemessages == 100);
		cl.move_resume_marker_epoch_valid = true;
		cl.move_resume_marker_epoch_sent = 5;
		cl.move_resume_marker_first_sequence = first + 2;
		length = moveack (packet, first, MOVEACK_FLAG_SELECTED,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 5,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		assert (parse (packet, length) && parse_ack_accepted &&
			!cl.move_ack_resume_pending && cl.ackedmovemessages == 100 && commandframe == 100);
		/* Pending is now false: only the existing uncompleted marker latch
		 * protects the repeated awaiting-completion packet's forward alias. */
		assert (parse (packet, length) && parse_ack_accepted &&
			!cl.move_ack_resume_pending && cl.move_resume_marker_epoch_valid &&
			cl.move_resume_marker_first_sequence == first + 2 &&
			cl.ackedmovemessages == 100 && commandframe == 100 &&
			!cl.net_move_acks && resume_resets == 1);
		length = moveack (packet, first, MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 5,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		assert (parse (packet, length) && parse_ack_accepted && resume_resets == 1 &&
			cl.move_resume_marker_epoch_valid && cl.ackedmovemessages == 100 &&
			commandframe == 100 && !cl.net_move_acks);
		length = moveack (packet, first + 2,
			MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_COMPLETED,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 5,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		assert (parse (packet, length) && parse_ack_accepted &&
			cl.ackedmovemessages == first + 2 && commandframe == first + 2 &&
			!cl.move_ack_resume_pending && cl.net_move_acks == 1);
		length = moveack (packet, first,
			MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 5,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		assert (parse (packet, length) && !parse_ack_accepted && resume_resets == 1 &&
			!cl.move_ack_resume_pending && cl.ackedmovemessages == first + 2);
	}
	reset_client ();
	cl.movemessages = 500;
	cl.ackedmovemessages = 100;
	cl.move_ack_selected_owner = true;
	cl.move_ack_discontinuity_epoch = 4;
	int length = moveack (packet, 100, MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 5,
		MOVEACK_DISCONTINUITY_GAP, false, false, 0);
	assert (parse (packet, length) && resume_resets == 1);
	cl.move_resume_marker_epoch_valid = true;
	cl.move_resume_marker_epoch_sent = 5;
	cl.move_resume_marker_first_sequence = 102;
	length = moveack (packet, 100, MOVEACK_FLAG_SELECTED,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 5,
		MOVEACK_DISCONTINUITY_GAP, false, false, 0);
	assert (parse (packet, length) && !cl.move_ack_resume_pending);
	length = moveack (packet, 100, MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 5,
		MOVEACK_DISCONTINUITY_GAP, false, false, 0);
	assert (parse (packet, length) && resume_resets == 1 &&
		cl.move_resume_marker_epoch_valid && cl.move_resume_marker_first_sequence == 102);
	reset_client ();
	puts ("Private recovery metadata: lost completion over half-range/full wrap and same-epoch regression passed");
}

static void assert_rejected_unchanged (const byte *bytes, int length)
{
	client_state_t before = cl;
	assert (!parse (bytes, length));
	assert (msg_badread);
	assert (!memcmp (&cl, &before, sizeof (cl)));
}

/* Exercise the production diagnostic lookup used by shownet and the malformed
 * command error path. This does not substitute a miniature service dispatcher. */
static void test_command_names (void)
{
	assert (!strcmp (CL_ServerCommandName (svc_nop), "svc_nop"));
	assert (!strcmp (CL_ServerCommandName (svc_localsound), "svc_localsound"));
	const int invalid[] = {INT_MIN, -1, 128, 255, INT_MAX};
	for (unsigned int index = 0; index < countof (invalid); index++)
		assert (!strcmp (CL_ServerCommandName (invalid[index]), "unknown"));
	for (int opcode = 57; opcode < 128; opcode++)
		if (opcode != svc_vrikpose && opcode != svc_voice)
			assert (!strcmp (CL_ServerCommandName (opcode), "unknown"));
	assert (!strcmp (CL_ServerCommandName (svc_vrikpose), "svc_vrikpose"));
	assert (!strcmp (CL_ServerCommandName (svc_voice), "svc_voice"));
	assert (!strcmp (CL_ServerCommandName (svcfte_updateentities), "unknown"));

	const byte ack_then_bad[] = {57, 1, 0, 3, 2, 2, 0, 3, 0, 0, 127};
	reset_client ();
	cl.movemessages = 2;
	net_message.data = (byte *)ack_then_bad;
	net_message.cursize = sizeof (ack_then_bad);
	MSG_BeginReading ();
	const int previous = MSG_ReadByte ();
	assert (previous == QSVR_SVC_MOVEACK);
	CL_ParseMoveAck (NULL);
	assert (!msg_badread && cl.ackedmovemessages == 1);
	assert (MSG_ReadByte () == 127 && msg_readcount == sizeof (ack_then_bad));
	assert (!strcmp (CL_ServerCommandName (previous), "unknown"));
}

static void test_demo_ack_consumption (void)
{
	byte bytes[160];
	cls.demoplayback = true;
	cls.netcon = NULL;
	for (int kind = 0; kind < 3; kind++)
	{
		reset_client ();
		assert (!cl.movemessages && !cl.vr_gorilla_supported &&
			!cl.vr_gorilla_trusted_cap_sent);
		unsigned flags = MOVEACK_FLAG_AUTHORITATIVE | MOVEACK_FLAG_PREDICTION_ALLOWED |
			MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_COMPLETED;
		if (kind) flags |= MOVEACK_FLAG_VR_GORILLA;
		if (kind == 2) flags |= MOVEACK_FLAG_GORILLA_TRUSTED;
		int length = moveack (bytes, 19, flags, MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT,
			2, 3, MOVEACK_DISCONTINUITY_NONE, kind == 2, kind != 0, 7);
		client_state_t before = cl;
		bytes[length] = svc_nop;
		assert (parse (bytes, length + 1) && !msg_badread);
		assert (!parse_ack_accepted && !memcmp (&cl, &before, sizeof (cl)));
		assert (!smoothing_resets && !resume_resets && !flushes);
		assert (msg_readcount == length && MSG_ReadByte () == svc_nop);
		for (int cut = 0; cut < length; cut++)
			assert_rejected_unchanged (bytes, cut);
	}
	/* Seed a resume marker and a completable cursor so the same body would
	 * actually reset the marker in live play. Playback must preserve both. */
	reset_client ();
	cl.movemessages = 20;
	cl.ackedmovemessages = 18;
	cl.move_ack_discontinuity_epoch = 2;
	cl.move_resume_marker_epoch_valid = true;
	cl.move_resume_marker_epoch_sent = 3;
	cl.move_resume_marker_first_sequence = 19;
	int length = moveack (bytes, 19, MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 2, 4, MOVEACK_DISCONTINUITY_GAP,
		false, false, 0);
	client_state_t resume_before = cl;
	bytes[length] = svc_nop;
	assert (parse (bytes, length + 1) && !msg_badread);
	assert (!parse_ack_accepted && !memcmp (&cl, &resume_before, sizeof (cl)));
	assert (!smoothing_resets && !resume_resets && !flushes);
	assert (msg_readcount == length && MSG_ReadByte () == svc_nop);
	cls.demoplayback = false;
	assert (parse (bytes, length) && parse_ack_accepted && resume_resets == 1);
	assert (!cl.move_resume_marker_epoch_valid && !cl.move_resume_marker_first_sequence);
	cls.demoplayback = true;
	reset_client ();
	length = moveack (bytes, 19, MOVEACK_FLAG_VR_GORILLA,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 2, 3, 0, false, true, 7);
	/* Complete invalid bodies still fail offline: a NaN anchor, then an
	 * out-of-range surface-model index. Only capability provenance is bypassed. */
	int offset = 16;
	put_float (bytes, &offset, NAN);
	assert_rejected_unchanged (bytes, length);
	length = moveack (bytes, 19, MOVEACK_FLAG_VR_GORILLA,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 2, 3, 0, false, true, QSVR_MODEL_LIMIT);
	assert_rejected_unchanged (bytes, length);
	cls.demoplayback = false;
	reset_client ();
	/* The identical complete body still requires live capability admission. */
	length = moveack (bytes, 19, MOVEACK_FLAG_VR_GORILLA,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 2, 3, 0, false, true, 7);
	assert_rejected_unchanged (bytes, length);
	length = moveack (bytes, 19, MOVEACK_FLAG_GORILLA_TRUSTED,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 2, 3, 0, true, false, 0);
	assert_rejected_unchanged (bytes, length);
	puts ("DEMO_MOVEACK_CONSUMPTION_PASSED body/framing/validation; no live replay state");
}

static void check_paused_generation_fence (void)
{
	byte packet[256];
	for (int wrap = 0; wrap < 2; ++wrap)
	{
		reset_client ();
		cl.movemessages = 110;
		cl.ackedmovemessages = 100;
		cl.move_ack_selected_owner = true;
		cl.move_ack_resume_pending = true;
		cl.move_ack_discontinuity_epoch = wrap ? 0xffff : 5;
		cl.move_resume_marker_epoch_valid = true;
		cl.move_resume_marker_epoch_sent = cl.move_ack_discontinuity_epoch;
		cl.move_resume_marker_first_sequence = 0;
		const unsigned old_epoch = cl.move_ack_discontinuity_epoch;
		int length = moveack (packet, 101,
			MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_COMPLETED | MOVEACK_FLAG_PREDICTION_ALLOWED,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, old_epoch,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		client_state_t before = cl;
		assert (parse (packet, length) && !parse_ack_accepted && !memcmp (&before, &cl, sizeof (cl)));
		assert (parsed_source_epoch == (int)old_epoch);
		length = moveack (packet, 101,
			MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_COMPLETED | MOVEACK_FLAG_PREDICTION_ALLOWED,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, (old_epoch + 1) & 0xffff,
			MOVEACK_DISCONTINUITY_RESET_TELEPORT, false, false, 0);
		assert (parse (packet, length) && !parse_ack_accepted && !memcmp (&before, &cl, sizeof (cl)));
		assert (parsed_source_epoch == (int)((old_epoch + 1) & 0xffff));
		length = moveack (packet, 100, MOVEACK_FLAG_SELECTED,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, old_epoch,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		assert (parse (packet, length) && !parse_ack_accepted && !memcmp (&before, &cl, sizeof (cl)));
		length = moveack (packet, 100, MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, (old_epoch + 1) & 0xffff,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		assert (parse (packet, length) && parse_ack_accepted && cl.move_ack_resume_pending &&
			!cl.move_ack_prediction_allowed && !cl.move_resume_marker_epoch_valid &&
			!cl.move_resume_marker_first_sequence && resume_resets == 1 &&
			cl.move_ack_discontinuity_epoch == ((old_epoch + 1) & 0xffff));
	}
	reset_client ();
	puts ("Private pause generation: stale completed/suspended metadata denied, fresh fence accepted across wrap");
}

int main (void)
{
	byte packet[160];
	int length;
	float servercommandframe = -1;

	test_command_names ();
	test_pending_epoch_after_lost_completion ();
	test_demo_ack_consumption ();

	reset_client ();
	cl.qcvm.extglobals.servercommandframe = &servercommandframe;
	cl.movemessages = 0x10002;
	cl.ackedmovemessages = 0x0fffe;
	length = moveack (packet, 1, MOVEACK_FLAG_AUTHORITATIVE | MOVEACK_FLAG_PREDICTION_ALLOWED,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 2, 3, MOVEACK_DISCONTINUITY_NONE, false, false, 0);
	assert (parse (packet, length));
	assert (!msg_badread && msg_readcount == length && parse_ack_accepted &&
		cl.ackedmovemessages == 0x10001 && cl.net_move_acks == 1);
	assert (servercommandframe == 0x10001);
	assert (cl.move_ack_mode_epoch == 2 && cl.move_ack_discontinuity_epoch == 3);
	assert (smoothing_resets == 1);

	/* Stale ACKs cannot restore old authoritative metadata. */
	client_state_t accepted = cl;
	length = moveack (packet, 0xffff, 0, MOVE_AUTHORITY_UNKNOWN, 99, 99, 9, false, false, 0);
	assert (parse (packet, length));
	assert (!msg_badread && !parse_ack_accepted && cl.net_move_stale_acks == 1);
	assert (!memcmp (&cl.move_ack_authority, &accepted.move_ack_authority,
		offsetof (client_state_t, move_msec_sample_time) - offsetof (client_state_t, move_ack_authority)));

	/* Equal ACKs retain sequence/counters but still refresh epochs and reset smoothing. */
	length = moveack (packet, 1, MOVEACK_FLAG_AUTHORITATIVE | MOVEACK_FLAG_PREDICTION_ALLOWED,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 4, 3, MOVEACK_DISCONTINUITY_GAP, false, false, 0);
	assert (parse (packet, length));
	assert (parse_ack_accepted && cl.ackedmovemessages == 0x10001 && cl.net_move_acks == 1);
	assert (cl.move_ack_mode_epoch == 4 && cl.move_ack_discontinuity_reason == MOVEACK_DISCONTINUITY_GAP);
	assert (smoothing_resets == 2);

	/* A low 16-bit wrap expands relative to the last completed command. */
	reset_client ();
	cl.movemessages = 0x20001;
	cl.ackedmovemessages = 0x1ffff;
	length = moveack (packet, 0, MOVEACK_FLAG_PREDICTION_ALLOWED,
		MOVE_AUTHORITY_LEGACY_FRAME, 1, 1, 0, false, false, 0);
	assert (parse (packet, length) && cl.ackedmovemessages == 0x20000);
	client_state_t before_long_pause = cl;

	/* A long pause can leave the completion cursor one entire 16-bit epoch
	 * behind generation. Its unchanged ACK must not falsely complete 65536
	 * commands or reopen replay against a nonexistent owner baseline. */
	reset_client ();
	cl.movemessages = 65636;
	cl.ackedmovemessages = 100;
	length = moveack (packet, 100,
		MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT,
		1, 2, MOVEACK_DISCONTINUITY_GAP, false, false, 0);
	assert (parse (packet, length) && cl.ackedmovemessages == 100 &&
		cl.net_move_acks == 0 && !cl.move_ack_prediction_allowed &&
		cl.move_ack_selected_owner && resume_resets == 1);
	/* A stale pre-wrap ACK stays stale even with a far-ahead producer. */
	length = moveack (packet, 99, 0, MOVE_AUTHORITY_UNKNOWN,
		1, 1, MOVEACK_DISCONTINUITY_NONE, false, false, 0);
	assert (parse (packet, length) && !parse_ack_accepted &&
		cl.ackedmovemessages == 100 && cl.net_move_stale_acks == 1);
	cl.move_resume_marker_epoch_valid = true;
	cl.move_resume_marker_epoch_sent = 2;
	cl.move_resume_marker_first_sequence = 65636;
	cl.movemessages = 65638;
	length = moveack (packet, 100,
		MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_COMPLETED,
		MOVE_AUTHORITY_LEGACY_FRAME, 1, 2, MOVEACK_DISCONTINUITY_GAP,
		false, false, 0);
	assert (parse (packet, length) && cl.ackedmovemessages == 65636 &&
		cl.move_ack_selected_owner && cl.net_move_acks == 1 &&
		resume_resets == 1);
	length = moveack (packet, 100, MOVEACK_FLAG_SELECTED,
		MOVE_AUTHORITY_UNKNOWN, 1, 2, MOVEACK_DISCONTINUITY_GAP,
		false, false, 0);
	assert (parse (packet, length) && cl.ackedmovemessages == 65636 &&
		cl.move_ack_authority == MOVE_AUTHORITY_LEGACY_FRAME);
	length = moveack (packet, 100, MOVEACK_FLAG_SELECTED,
		MOVE_AUTHORITY_UNKNOWN, 1, 1, MOVEACK_DISCONTINUITY_NONE,
		false, false, 0);
	assert (parse (packet, length) && cl.ackedmovemessages == 65636 &&
		cl.move_ack_discontinuity_epoch == 2 &&
		cl.move_ack_authority == MOVE_AUTHORITY_LEGACY_FRAME);
	reset_client ();
	cl.movemessages = 40002;
	cl.ackedmovemessages = 100;
	cl.move_resume_marker_epoch_valid = true;
	cl.move_resume_marker_epoch_sent = 4;
	cl.move_resume_marker_first_sequence = 40000;
	length = moveack (packet, 40000,
		MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_COMPLETED,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 4, MOVEACK_DISCONTINUITY_GAP,
		false, false, 0);
	assert (parse (packet, length) && cl.ackedmovemessages == 40000);
	reset_client ();
	cl.movemessages = 500;
	cl.ackedmovemessages = 100;
	cl.move_ack_selected_owner = true;
	cl.move_ack_discontinuity_epoch = 4;
	length = moveack (packet, 100,
		MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING,
		MOVE_AUTHORITY_LEGACY_FRAME, 1, 5,
		MOVEACK_DISCONTINUITY_RESET_TELEPORT, false, false, 0);
	assert (parse (packet, length) && resume_resets == 1 &&
		cl.move_ack_resume_pending &&
		cl.move_ack_discontinuity_reason == MOVEACK_DISCONTINUITY_RESET_TELEPORT);
	cl = before_long_pause;
	cl.movemessages = 0x20002;

	/* The selected private layout always requires the extended payload. */
	cl.protocol_qsvr = 0;
	assert_rejected_unchanged (packet, length);
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	for (int flag = MOVEACK_FLAG_AUTHORITATIVE; flag <= MOVEACK_FLAG_PREDICTION_ALLOWED; flag <<= 1)
	{
		length = moveack (packet, 1,
			MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING | flag,
			MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 1,
			MOVEACK_DISCONTINUITY_GAP, false, false, 0);
		assert_rejected_unchanged (packet, length);
	}
	length = moveack (packet, 1,
		MOVEACK_FLAG_SELECTED | MOVEACK_FLAG_RESUME_PENDING |
		MOVEACK_FLAG_RESUME_COMPLETED,
		MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT, 1, 1,
		MOVEACK_DISCONTINUITY_GAP, false, false, 0);
	assert_rejected_unchanged (packet, length);

	/* Gorilla state requires both capability and a valid private model identity. */
	length = moveack (packet, 1, MOVEACK_FLAG_PREDICTION_ALLOWED |
		MOVEACK_FLAG_VR_GORILLA | MOVEACK_FLAG_GORILLA_TRUSTED,
		MOVE_AUTHORITY_PMOVE_QC_COMMAND, 7, 8, 0, true, true, QSVR_MODEL_LIMIT - 1);
	assert_rejected_unchanged (packet, length);
	cl.vr_gorilla_trusted_cap_sent = true;
	assert_rejected_unchanged (packet, length);
	cl.vr_gorilla_supported = true;
	assert (parse (packet, length));
	assert (cl.vr_gorilla_state_valid && cl.vr_gorilla_state_sequence == 19);
	assert (cl.vr_gorilla_motion_generation_valid && cl.vr_gorilla_motion_generation == 0x12345678u);
	assert (cl.vr_gorilla_state.surface[1] == 42 && cl.vr_gorilla_state.surface_model[1] == QSVR_MODEL_LIMIT - 1);

	/* Every truncated prefix fails before accepted ACK/metadata/Gorilla state changes. */
	for (int prefix = 0; prefix < length; prefix++)
	{
		client_state_t before = cl;
		assert (!parse (packet, prefix));
		assert (parsed_source_epoch == -1);
		assert (msg_badread && !memcmp (&cl, &before, sizeof (cl)));
	}
	length = moveack (packet, 2, MOVEACK_FLAG_PREDICTION_ALLOWED | MOVEACK_FLAG_VR_GORILLA,
		MOVE_AUTHORITY_PMOVE_QC_COMMAND, 7, 8, 0, false, true, QSVR_MODEL_LIMIT);
	assert_rejected_unchanged (packet, length);
	length = moveack (packet, 2, MOVEACK_FLAG_PREDICTION_ALLOWED | MOVEACK_FLAG_VR_GORILLA,
		MOVE_AUTHORITY_PMOVE_QC_COMMAND, 7, 8, 0, false, true, QSVR_MODEL_LIMIT - 1);
	packet[16] = 0;
	packet[17] = 0;
	packet[18] = 0x80;
	packet[19] = 0x7f;
	assert_rejected_unchanged (packet, length);

	/* Queue boundaries are fixture-only transport stand-ins; queue ownership is production. */
	cls.netcon = &socket_stub;
	cl.ackframes_count = 0;
	CLFTE_QueueAckFrame (77);
	CLFTE_QueueAckFrame (77);
	assert (cl.ackframes_count == 1 && cl.ackframes[0] == 77);
	cl.ackframes_count = CL_ACKFRAME_HISTORY - 1;
	CLFTE_QueueAckFrame (78);
	assert (cl.ackframes_count == CL_ACKFRAME_HISTORY && flushes == 1);
	CLFTE_QueueAckFrame (79);
	assert (cl.net_snapshot_ack_queue_overflows == 1);
	check_paused_generation_fence ();

	puts ("Private move ACK: production parsing, state atomicity, wrap, Gorilla and queue checks passed");
}
