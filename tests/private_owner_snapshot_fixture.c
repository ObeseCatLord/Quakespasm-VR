/* Private owner association through the production update and ACK readers.
 * The fixture calls the real message-end commit helper directly; the main
 * parser's outer CL_ParseServerMessage dispatch is covered by a separate probe. */
#include "../Quake/cl_parse.c"
#include "../Quake/pmove.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

client_state_t cl;
client_static_t cls;
server_t sv;
qcvm_t *qcvm;
sizebuf_t net_message;
cvar_t cl_shownet;
cvar_t v_gunkick;
viddef_t vid;
int r_trace_line_cache_counter;
vec3_t v_punchangles[2];
double v_punchangles_times[2];

struct qsocket_s { int unused; };
int NET_QSocketGetSequenceIn (const struct qsocket_s *sock) { return 1; }
void CL_ResetPredictionSmoothing (void) {}
void CL_FlushAckFrames (void) {}
void R_TranslateNewPlayerSkin (int playernum) {}
void R_FreeEntityBLAS (entity_t *ent) {}
int VectorCompare (const vec3_t first, const vec3_t second)
{
	return first[0] == second[0] && first[1] == second[1] && first[2] == second[2];
}
void CL_SignonReply (void) { assert (!"unexpected signon reply"); }
void Con_DPrintf (const char *fmt, ...) {}
void Con_DWarning (const char *fmt, ...) {}
void Con_Printf (const char *fmt, ...) {}
void Con_SafePrintf (const char *fmt, ...) {}
void Host_Error (const char *fmt, ...) { assert (!"unexpected Host_Error"); }
void Host_EndGame (const char *fmt, ...) { assert (!"unexpected Host_EndGame"); }
void PR_SwitchQCVM (qcvm_t *vm) { assert (!"unexpected QC switch"); }
const char *PR_GetString (int n) { assert (!"unexpected QC string lookup"); return ""; }
edict_t *EDICT_NUM (int n) { assert (!"unexpected edict lookup"); return NULL; }

static entity_t entities[4];

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

static int private_snapshot (byte *bytes, unsigned ack, unsigned mode_epoch,
	unsigned owner, qboolean include_owner, qboolean remove_owner,
	float server_time)
{
	int length = 0;
	put_short (bytes, &length, ack);
	put_byte (bytes, &length, MOVEACK_FLAG_AUTHORITATIVE | MOVEACK_FLAG_PREDICTION_ALLOWED);
	put_byte (bytes, &length, MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT);
	put_short (bytes, &length, mode_epoch);
	put_short (bytes, &length, 3);
	put_byte (bytes, &length, MOVEACK_DISCONTINUITY_NONE);
	put_float (bytes, &length, server_time);
	if (remove_owner)
		put_short (bytes, &length, 0x8000 | owner);
	else if (include_owner)
	{
		put_short (bytes, &length, owner);
		put_byte (bytes, &length, UF_EXTEND1 | UF_PREDINFO);
		put_byte (bytes, &length, UF_RESET >> 8);
		put_byte (bytes, &length, UFP_VELOCITYXY | UFP_VELOCITYZ);
		put_short (bytes, &length, 64);
		put_short (bytes, &length, (unsigned short)-32);
		put_short (bytes, &length, 8);
	}
	put_short (bytes, &length, 0); // end of replacement entity list
	return length;
}

static void reset_client (void)
{
	memset (&cl, 0, sizeof (cl));
	memset (&cls, 0, sizeof (cls));
	memset (entities, 0, sizeof (entities));
	cl.protocol_qsvr = QSVR_PROTOCOL_PINNED;
	cl.protocol_pext2 = QSVR_PEXT2_REQUIRED; // the pinned selection includes PREDINFO
	cl.movemessages = 10;
	cl.ackedmovemessages = -1;
	cl.viewentity = 1;
	cl.max_edicts = countof (entities);
	cl.num_entities = 2;
	cl.maxclients = 1;
	cl.entities = entities;
	cl.mtime[0] = 2;
	cl.mtime[1] = 1.9;
	cl.model_precache[0] = NULL;
	entities[1].baseline.origin[0] = 12;
	entities[1].baseline.origin[1] = 34;
	entities[1].baseline.origin[2] = 56;
	entities[1].baseline.angles[0] = 90;
	entities[1].baseline.angles[1] = 180;
	entities[1].baseline.angles[2] = 270;
	entities[1].baseline.velocity[0] = 64;
	entities[1].baseline.velocity[1] = -32;
	entities[1].baseline.velocity[2] = 8;
	entities[1].netstate = entities[1].baseline;
	entities[1].update_type = true;
	entities[1].msgtime = cl.mtime[1];
	CL_InvalidateMoveSnapshot ();
}

static void parse_private_update_with_jump_secs (const byte *bytes, int length,
	qboolean include_jump_secs, float jump_secs)
{
	/* Model the complete movement-stat group preceding each private owner
	 * update. This fixture calls the entity reader directly, so it supplies
	 * the stat receipts through the production numeric parser. */
	cl_move_stat_receipts = 0;
	CL_ParseStatInt (STAT_MOVEFLAGS, MOVEFLAG_VALID);
	for (int stat = STAT_MOVEVARS_WATERSINKSPEED; stat <= STAT_MOVEVARS_KTJUMP; stat++)
		CL_ParseStatFloat (stat, 0);
	for (int stat = STAT_MOVEVARS_FRICTION; stat <= STAT_MOVEVARS_WATERFRICTION; stat++)
		CL_ParseStatFloat (stat, 0);
	for (int stat = STAT_MOVEVARS_TIMESCALE; stat <= STAT_MOVEVARS_STEPHEIGHT; stat++)
		CL_ParseStatFloat (stat, 0);
	if (include_jump_secs)
		CL_ParseStatFloat (STAT_PRIVATE_JUMP_SECS, jump_secs);
	net_message.data = (byte *)bytes;
	net_message.cursize = length;
	MSG_BeginReading ();
	CLFTE_ParseEntitiesUpdate ();
}

static void parse_private_update (const byte *bytes, int length)
{
	parse_private_update_with_jump_secs (bytes, length, true, 0.0f);
}

static void finish_message_if_complete (int length)
{
	if (!msg_badread && msg_readcount == length)
		CLFTE_CommitMoveSnapshot ();
}

static int make_standalone_ack (byte *bytes, unsigned ack, unsigned epoch)
{
	int length = 0;
	put_short (bytes, &length, ack);
	put_byte (bytes, &length, MOVEACK_FLAG_AUTHORITATIVE | MOVEACK_FLAG_PREDICTION_ALLOWED);
	put_byte (bytes, &length, MOVE_AUTHORITY_PMOVE_ENGINE_COMPAT);
	put_short (bytes, &length, epoch);
	put_short (bytes, &length, 3);
	put_byte (bytes, &length, MOVEACK_DISCONTINUITY_NONE);
	return length;
}

int main (void)
{
	byte packet[64];
	int length;

	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	net_message.data = packet;
	net_message.cursize = length;
	MSG_BeginReading ();
	CLFTE_ParseEntitiesUpdate ();
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid); // owner/ACK without this message's settings is not eligible

	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	parse_private_update_with_jump_secs (packet, length, false, 0.0f);
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid); // the timer stat is required even when its value is zero

	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	parse_private_update_with_jump_secs (packet, length, true, -0.25f);
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid); // negative debounce state is unusable

	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	parse_private_update_with_jump_secs (packet, length, true, NAN);
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid); // nonfinite debounce state is unusable

	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	parse_private_update (packet, length);
	assert (!msg_badread && msg_readcount == length);
	assert (!cl.move_snapshot_valid); // candidate is private until message end
	finish_message_if_complete (length);
	assert (cl.move_snapshot_valid && cl.move_snapshot_ack == 10 && cl.move_snapshot_owner == 1);
	assert (cl.statsf[STAT_PRIVATE_JUMP_SECS] == 0.0f); // receipt does not depend on a nonzero value
	assert (entities[1].netstate.origin[0] == 12 && entities[1].netstate.velocity[0] == 64);
	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	parse_private_update_with_jump_secs (packet, length, true, 0.125f);
	finish_message_if_complete (length);
	assert (cl.move_snapshot_valid && cl.statsf[STAT_PRIVATE_JUMP_SECS] == 0.125f);

	/* An omitted unreliable fragment cannot poison the next self-contained
	 * repeated owner reset: deliberately seed the previous decoded state stale. */
	entities[1].netstate.origin[0] = 900;
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.1f);
	parse_private_update (packet, length);
	assert (!cl.move_snapshot_valid);
	finish_message_if_complete (length);
	assert (cl.move_snapshot_valid && entities[1].netstate.origin[0] == 12);
	/* A continuation of that same snapshot must not collapse interpolation
	 * history when it repeats an otherwise unchanged owner reset. */
	float prior_origin = entities[1].msg_origins[1][0];
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.1f);
	parse_private_update (packet, length);
	finish_message_if_complete (length);
	assert (cl.move_snapshot_valid && entities[1].msg_origins[1][0] == prior_origin);

	/* A stale but syntactically valid ACK is not an accepted owner association. */
	reset_client ();
	cl.ackedmovemessages = 5;
	length = private_snapshot (packet, 4, 4, 1, true, false, 3.0f);
	parse_private_update (packet, length);
	finish_message_if_complete (length);
	assert (!msg_badread && cl.net_move_stale_acks == 1 && !cl.move_snapshot_valid);

	/* Equal ACKs can refresh epochs inside the owner message and still pair. */
	reset_client ();
	cl.ackedmovemessages = 10;
	cl.move_ack_authority = MOVE_AUTHORITY_LEGACY_FRAME;
	length = private_snapshot (packet, 10, 9, 1, true, false, 3.0f);
	parse_private_update (packet, length);
	finish_message_if_complete (length);
	assert (cl.move_snapshot_valid && cl.move_ack_mode_epoch == 9);

	/* A later standalone ACK may update authority but cannot inherit this owner. */
	length = make_standalone_ack (packet, 10, 10);
	net_message.data = packet;
	net_message.cursize = length;
	MSG_BeginReading ();
	qboolean accepted = false;
	assert (CL_ParseMoveAckPayload (&accepted) && accepted);
	assert (!cl.move_snapshot_valid);

	/* Omission, owner removal, world reset, and changed viewentity all fail closed. */
	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, false, false, 3.0f);
	parse_private_update (packet, length);
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid);
	length = private_snapshot (packet, 10, 4, 1, false, true, 3.1f);
	parse_private_update (packet, length);
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid && !entities[1].update_type);
	reset_client ();
	length = private_snapshot (packet, 10, 4, 0, false, true, 3.2f);
	parse_private_update (packet, length); // world removal resets every entity
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid && !entities[1].update_type);

	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	parse_private_update (packet, length);
	CL_InvalidateMoveSnapshot (); // svc_setview changing away from entity 1
	cl.viewentity = 2;
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid);

	reset_client ();
	entities[1].baseline.origin[0] = NAN;
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	parse_private_update (packet, length);
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid);

	reset_client ();
	length = private_snapshot (packet, 10, 4, 1, true, false, NAN);
	parse_private_update (packet, length);
	finish_message_if_complete (length);
	assert (!cl.move_snapshot_valid && cl.mtime[0] == 2);

	/* Every logical truncation prefix (bounded cursize, intact backing array)
	 * must fail closed before boundary commit. */
	length = private_snapshot (packet, 10, 4, 1, true, false, 3.0f);
	for (int prefix = 0; prefix < length; prefix++)
	{
		reset_client ();
		parse_private_update (packet, prefix);
		finish_message_if_complete (prefix);
		assert (!cl.move_snapshot_valid);
	}

	/* Public replacement framing and entity decoding remain on their old path. */
	reset_client ();
	cl.protocol_qsvr = 0;
	cl.protocol_pext2 = PEXT2_REPLACEMENTDELTAS;
	CL_ParseStatFloat (STAT_PRIVATE_JUMP_SECS, 2.5f);
	assert (cl.stats[STAT_PRIVATE_JUMP_SECS] == 2 &&
		cl.statsf[STAT_PRIVATE_JUMP_SECS] == 2.5f); // public stat 254 keeps donor parsing
	int public_length = 0;
	put_float (packet, &public_length, 3.0f);
	put_short (packet, &public_length, 1);
	put_byte (packet, &public_length, UF_EXTEND1);
	put_byte (packet, &public_length, UF_RESET >> 8);
	put_short (packet, &public_length, 0);
	parse_private_update (packet, public_length);
	assert (!msg_badread && msg_readcount == public_length);
	assert (entities[1].update_type && !cl.move_snapshot_valid);

	puts ("Private owner snapshot: production ACK/delta readers and boundary association passed");
	return 0;
}
