/* Production delta decoder + production MSG readers; no simulated wire reader. */
#include "../Quake/cl_parse.c"
#include "../Quake/pmove.h"
#undef VectorNegate
#include "../Quake/world.c"
#include <assert.h>
#include <setjmp.h>

client_state_t	cl;
client_static_t cls;
server_t		sv;
qcvm_t		   *qcvm;
sizebuf_t		net_message;
cvar_t			cl_shownet;
cvar_t			pr_checkextension;
extern cvar_t	sv_fte_recursivehullckeck;
int				r_trace_line_cache_counter;
cvar_t			v_gunkick;
vec3_t			v_punchangles[2];
double			v_punchangles_times[2];
int				NET_QSocketGetSequenceIn (const struct qsocket_s *sock)
{
	abort ();
}
void R_TranslateNewPlayerSkin (int playernum)
{
	abort ();
}
void R_FreeEntityBLAS (entity_t *ent)
{
	abort ();
}
void CL_SignonReply (void)
{
	abort ();
}
// This fixture exercises public removal packets, not private ACK traffic.
void CL_ResetPredictionSmoothing (void) { abort (); }
void CL_FlushAckFrames (void) { abort (); }
static jmp_buf	parse_error;
static qboolean expect_error;
void			Host_Error (const char *fmt, ...)
{
	assert (expect_error);
	longjmp (parse_error, 1);
}
void Host_EndGame (const char *fmt, ...)
{
	Host_Error (fmt);
}
void Sys_Error (const char *fmt, ...)
{
	abort ();
}
void Con_SafePrintf (const char *fmt, ...) {}
void Con_DPrintf (const char *fmt, ...) {}
void Con_Printf (const char *fmt, ...) {}
// Diagnostic-only missing-reset branch is never used by these cases.
void PR_SwitchQCVM (qcvm_t *vm)
{
	abort ();
}
const char *PR_GetString (int n)
{
	abort ();
}
edict_t *EDICT_NUM (int n)
{
	abort ();
}

static entity_state_t decode (const byte *bytes, int length, unsigned layout)
{
	entity_state_t old = {0}, result = {0}, baseline = {0};
	old.solidsize = 0x80201810;
	cl.protocol_qsvr = layout;
	net_message.data = (byte *)bytes;
	net_message.cursize = length;
	MSG_BeginReading ();
	CLFTE_ReadDelta (1, &result, &old, &baseline);
	return result;
}
static void valid (const byte *bytes, int length, unsigned layout, unsigned solid)
{
	entity_state_t result = decode (bytes, length, layout);
	assert (!msg_badread && result.solidsize == solid);
	assert (msg_readcount == length - 1 && MSG_ReadByte () == 0x5a);
}
int main (void)
{
	// UF_EXTEND1 + UF_SOLID, followed by one solid and a next-opcode sentinel.
	const byte public_hull[] = {0x80, 0x20, 0x62, 0x20, 0x5a};
	const byte public_none[] = {0x80, 0x20, 0, 0, 0x5a};
	const byte public_bsp[] = {0x80, 0x20, 0x1f, 0, 0x5a};
	const byte unchanged[] = {0, 0x5a};
	valid (public_hull, sizeof public_hull, 0, ES_SOLID_HULL1);
	valid (public_bsp, sizeof public_bsp, 0, ES_SOLID_BSP);
	valid (public_none, sizeof public_none, 0, ES_SOLID_NOT);
	valid (unchanged, sizeof unchanged, 0, ES_SOLID_HULL1);
	// A colliding private-looking FTE bitmask is not dialect admission.
	cl.protocol_pext2 = QSVR_PEXT2_REQUIRED;
	valid (public_hull, sizeof public_hull, 0, ES_SOLID_HULL1);
	const unsigned solids[] = {ES_SOLID_NOT, ES_SOLID_BSP, ES_SOLID_HULL1, ES_SOLID_HULL2};
	for (int tag = 0; tag < 4; ++tag)
	{
		byte packet[] = {0x80, 0x20, (byte)tag, 0x5a};
		valid (packet, sizeof packet, QSVR_PROTOCOL_PINNED, solids[tag]);
	}
	const byte compact_none[] = {0x80, 0x20, 16, 0, 0, 0x5a};
	const byte compact[] = {0x80, 0x20, 16, 0x62, 0x20, 0x5a};
	const byte extended[] = {0x80, 0x20, 32, 0x10, 0x18, 0x20, 0x80, 0x5a};
	valid (compact, sizeof compact, QSVR_PROTOCOL_PINNED, ES_SOLID_HULL1);
	valid (compact_none, sizeof compact_none, QSVR_PROTOCOL_PINNED, ES_SOLID_NOT);
	valid (extended, sizeof extended, QSVR_PROTOCOL_PINNED, ES_SOLID_HULL1);
	valid (unchanged, sizeof unchanged, QSVR_PROTOCOL_PINNED, ES_SOLID_HULL1);
	// Decoded bounds reach the inherited read-only weapon collision consumer,
	// using the real donor hull tracer and a synthetic empty world + entity box.
	qmodel_t world = {0};
	world.type = mod_brush;
	world.hulls[0].firstclipnode = CONTENTS_EMPTY;
	entity_t entities[2] = {0};
	cl.worldmodel = &world;
	cl.entities = entities;
	cl.num_entities = 2;
	cl.viewentity = 0;
	vec3_t start = {0, 0, 0}, end = {128, 0, 0};
	pr_checkextension.value = sv_fte_recursivehullckeck.value = 1;
	for (int private = 0; private < 2; ++private)
	{
		entities[1].netstate = private ? decode (extended, sizeof extended, QSVR_PROTOCOL_PINNED) : decode (public_hull, sizeof public_hull, 0);
		entities[1].netstate.origin[0] = 64;
		cl_weapon_trace_t trace = CL_TraceWeapon (start, end);
		assert (trace.entity == 1 && isfinite (trace.endpos[0]) && fabsf (trace.endpos[0] - 48) < .04f);
		assert (!trace.startsolid && !trace.allsolid && trace.fraction < 1);
		/* Production two-stage resolver must retract a held muzzle from this
		 * received solid without moving the player body or inventing a clear
		 * endpoint beyond the near face. */
		vec3_t torso = {0, 0, 0}, grip = {32, 0, 0};
		vec3_t tip = {80, 0, 0}, delta;
		assert (CL_ResolveWeaponCollision (torso, grip, grip, tip, delta));
		assert (delta[0] < -31.9f && delta[0] > -32.1f);
		assert (delta[1] == 0 && delta[2] == 0);
		assert (grip[0] == 32 && tip[0] == 80);
	}
	for (int private = 0; private < 2; ++private)
	{
		entities[1].netstate = private ? decode (compact_none, sizeof compact_none, QSVR_PROTOCOL_PINNED) : decode (public_none, sizeof public_none, 0);
		entities[1].netstate.origin[0] = 64;
		cl_weapon_trace_t trace = CL_TraceWeapon (start, end);
		assert (!trace.startsolid && !trace.allsolid && trace.fraction == 1);
		assert (isfinite (trace.endpos[0]) && fabsf (trace.endpos[0] - 128) < .04f);
	}
	// Execute the actual replacement-update removal owner at the same timestamp.
	// The weapon query cannot use msgtime alone to reject such stale bounds.
	cl.protocol_pext2 = 0;
	for (int reset_all = 0; reset_all < 2; ++reset_all)
	{
		byte removal[] = {0, 0, 0, 0, (byte)(reset_all ? 0 : 1), 0x80, 0, 0};
		entities[1].netstate = decode (public_hull, sizeof public_hull, 0);
		entities[1].netstate.origin[0] = 64;
		entities[1].update_type = true;
		net_message.data = removal;
		net_message.cursize = sizeof removal;
		MSG_BeginReading ();
		int cache_before = r_trace_line_cache_counter;
		CLFTE_ParseEntitiesUpdate ();
		assert (!msg_badread && msg_readcount == sizeof removal);
		assert (!entities[1].update_type && !entities[1].model);
		assert (entities[1].netstate.solidsize == ES_SOLID_NOT);
		assert (r_trace_line_cache_counter == cache_before + 1);
		cl_weapon_trace_t trace = CL_TraceWeapon (start, end);
		assert (!trace.startsolid && !trace.allsolid && trace.fraction == 1);
	}
	// Negative upper Z must stay signed in the donor network collision owner.
	const byte below_origin[] = {0x80, 0x20, 0x62, 0x0c, 0x5a};
	entities[1].netstate = decode (below_origin, sizeof below_origin, 0);
	entities[1].origin[0] = 64;
	qmodel_t box_model = {0};
	box_model.type = mod_alias;
	entities[1].model = &box_model;
	qcvm = &cl.qcvm; // World_ClipToNetwork is called only under the client VM.
	SV_InitBoxHull ();
	for (int sample = 0; sample < 4; ++sample)
	{
		int inside = sample & 1;
		sv_fte_recursivehullckeck.value = sample >> 1;
		start[2] = end[2] = inside ? -16 : 0;
		moveclip_t clip = {0};
		clip.start = start;
		clip.end = end;
		clip.mins = clip.maxs = vec3_origin;
		clip.hitcontents = CONTENTMASK_FROMQ1 (CONTENTS_SOLID);
		clip.trace.fraction = 1;
		VectorCopy (end, clip.trace.endpos);
		World_ClipToNetwork (&clip);
		assert (isfinite (clip.trace.fraction));
		assert (inside ? clip.trace.fraction < 1 : clip.trace.fraction == 1);
		assert (!clip.trace.startsolid && !clip.trace.allsolid);
	}
	// Unknown tags and unknown selected layouts never fall back to public data.
	const byte invalid[] = {0x80, 0x20, 9, 0x5a};
	expect_error = true;
	if (!setjmp (parse_error))
	{
		decode (invalid, sizeof invalid, QSVR_PROTOCOL_PINNED);
		assert (0);
	}
	if (!setjmp (parse_error))
	{
		decode (extended, sizeof extended, 2);
		assert (0);
	}
	expect_error = false;
	for (int length = 3; length < 7; ++length)
	{
		decode (extended, length, QSVR_PROTOCOL_PINNED);
		assert (msg_badread);
	}
	puts ("Solid deltas: retained public bounds, private tags, explicit dialect, framing and malformed bodies passed");
}
