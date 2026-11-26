/*
 * The workshop's tools, server side: the platform gun, the festive grapple and flares thrown off a
 * VR wrist. Everything runs in the engine, so a stock progs.dat needs no changes: a tool takes the
 * weapon slot on screen while it is out (SV_Workshop_ViewModel), holds QuakeC's own weapon back by
 * keeping attack_finished ahead of time, and does its damage through the progs' own T_Damage.
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * Platform gun, after the Engineer's in Deep Rock Galactic (numbers read from its weapon and
 * projectile assets, then scaled to Quake: 1 unit is about 3.2 cm):
 *   4 platforms a magazine, 16 carried, one shot a second, a 2 second automatic reload; the shot
 *   flies at 4000 cm/s under half gravity and a platform forms a quarter second after it lands.
 * Here the shot is a blob of gingerbread dough. On a wall, floor or ceiling it spreads into a flat
 * disk of gingerbread added to the cave (a terrain op, so it is networked and walkable like any
 * other ground); on a monster or player it shatters and hurts them.
 *
 * Grapple: a star-shaped hook on a string of Christmas lights. Hold fire to throw it; once it
 * bites into the cave or the map it pulls the player in, and letting go reels it back. A monster
 * it hits takes a little damage. Clients draw the light string from the thrower to the hook
 * (cl_workshop.c); the hook entity's skin carries the thrower's player number for that.
 */
#include "quakedef.h"
#include "terrain.h"

#define PLATGUN_VIEWMODEL "progs/workshop/v_platgun.mdl"
#define PLATGUN_BLOB	  "progs/workshop/gingerblob.mdl"
#define PLATGUN_SHARD	  "progs/workshop/gingershard.mdl"
#define GRAPPLE_VIEWMODEL "progs/workshop/v_grapple.mdl"
#define GRAPPLE_HOOK	  "progs/workshop/starhook.mdl"
#define GRAPPLE_BULB	  "progs/workshop/bulb.mdl"
#define WRISTBAND_MODEL	  "progs/workshop/wristband.mdl"

// QuakeC's sound channels and attenuation (defs.qc)
#define CHAN_AUTO	0
#define CHAN_WEAPON 1
#define CHAN_ITEM	3
#define ATTN_NORM	1

extern cvar_t sv_gravity;

enum
{
	TOOL_NONE,
	TOOL_PLATGUN,
	TOOL_GRAPPLE
};

#define IMPULSE_PLATGUN 70
#define IMPULSE_GRAPPLE 71
#define MAX_BLOBS		32
#define MAX_GROWS		32

static cvar_t workshop_tools = {"workshop_tools", "1", CVAR_NONE}; // the tools on drg* maps
static cvar_t workshop_platform_radius = {"workshop_platform_radius", "60", CVAR_NONE};
static cvar_t workshop_platform_thickness = {"workshop_platform_thickness", "20", CVAR_NONE};
static cvar_t workshop_platgun_speed = {"workshop_platgun_speed", "1250", CVAR_NONE};
static cvar_t workshop_platgun_damage = {"workshop_platgun_damage", "40", CVAR_NONE};
static cvar_t workshop_platgun_regen = {"workshop_platgun_regen", "10", CVAR_NONE}; // seconds per platform restored; 0 none
static cvar_t workshop_grapple_speed = {"workshop_grapple_speed", "1100", CVAR_NONE};
static cvar_t workshop_grapple_pull = {"workshop_grapple_pull", "620", CVAR_NONE};
static cvar_t workshop_grapple_range = {"workshop_grapple_range", "1600", CVAR_NONE};
static cvar_t workshop_flares = {"workshop_flares", "4", CVAR_NONE};			// flares a VR wrist carries
static cvar_t workshop_flare_regen = {"workshop_flare_regen", "15", CVAR_NONE}; // seconds per flare restored

#define PLATGUN_CLIP	  4
#define PLATGUN_MAX		  16
#define PLATGUN_RATE	  1.0
#define PLATGUN_RELOAD	  2.0
#define PLATFORM_DELAY	  0.25
#define PLATGUN_GRAVITY	  0.5f
#define PLATGUN_BLOB_LIFE 4.0

typedef struct
{
	int		 tool;
	int		 clip, reserve;
	double	 next_fire, reload_until, regen_at;
	double	 fire_frame_until;
	edict_t *hook;
	int		 hook_state; // 0 none, 1 flying, 2 holding
	vec3_t	 anchor;
	int		 flares;
	double	 flare_regen_at;
	int		 flares_sent;
} ws_player_t;

typedef struct
{
	edict_t *ent;
	int		 owner; // player number
	double	 born;
} ws_blob_t;

typedef struct
{
	vec3_t center, normal;
	double start;
	int	   stage;
} ws_grow_t;

static ws_player_t ws_players[MAX_SCOREBOARD + 1];
static ws_blob_t   ws_blobs[MAX_BLOBS];
static ws_grow_t   ws_grows[MAX_GROWS];
static int		   ws_num_blobs, ws_num_grows;
static qboolean	   ws_precached, ws_sweep;
static int		   ws_attack_finished = -1; // its offset in the running progs, looked up as a map loads

static void WS_ResetSlot (ws_player_t *p, qboolean release_hook);

static const char *WS_Model (const char *own, const char *fallback)
{
	return COM_FileExists (own, NULL) ? own : fallback;
}

static const char *WS_GrappleViewModel (void)
{
	return COM_FileExists (GRAPPLE_VIEWMODEL, NULL) ? GRAPPLE_VIEWMODEL
													: (COM_FileExists ("progs/v_grpple.mdl", NULL) ? "progs/v_grpple.mdl" : "progs/v_nail.mdl");
}

static const char *WS_HookModel (void)
{
	return COM_FileExists (GRAPPLE_HOOK, NULL) ? GRAPPLE_HOOK : (COM_FileExists ("progs/hook.mdl", NULL) ? "progs/hook.mdl" : "progs/spike.mdl");
}

void SV_Workshop_Clear (void)
{
	memset (ws_players, 0, sizeof (ws_players));
	ws_num_blobs = ws_num_grows = 0;
	ws_precached = false;
}

static qboolean WS_Active (void)
{
	return terrain_active && workshop_tools.value && ws_precached;
}

void SV_Workshop_Precache (void)
{
	SV_Workshop_Clear ();
	ws_attack_finished = ED_FindFieldOffset ("attack_finished");
	if (!workshop_tools.value)
		return;
	SV_Precache_Model (WS_Model (PLATGUN_VIEWMODEL, "progs/v_rock.mdl"));
	SV_Precache_Model (WS_Model (PLATGUN_BLOB, "progs/grenade.mdl"));
	SV_Precache_Model (WS_Model (PLATGUN_SHARD, "progs/gib1.mdl"));
	SV_Precache_Model (WS_GrappleViewModel ());
	SV_Precache_Model (WS_HookModel ());
	if (COM_FileExists (GRAPPLE_BULB, NULL))
		SV_Precache_Model (GRAPPLE_BULB);
	if (COM_FileExists (WRISTBAND_MODEL, NULL))
		SV_Precache_Model (WRISTBAND_MODEL);
	SV_Precache_Sound ("weapons/grenade.wav");
	SV_Precache_Sound ("zombie/z_hit.wav");
	SV_Precache_Sound ("zombie/z_gib.wav");
	SV_Precache_Sound ("weapons/tink1.wav");
	SV_Precache_Sound ("weapons/ax1.wav");
	SV_Precache_Sound ("weapons/lock4.wav");
	SV_Precache_Sound ("weapons/pkup.wav");
	ws_precached = true;
	ws_sweep = true;
	for (int i = 1; i <= MAX_SCOREBOARD; ++i)
		WS_ResetSlot (&ws_players[i], false);
}

// T_Damage (targ, inflictor, attacker, damage) from the running progs.
static void WS_Damage (edict_t *targ, edict_t *inflictor, edict_t *attacker, float damage)
{
	dfunction_t *f = ED_FindFunction ("T_Damage");
	if (!f || !targ->v.takedamage)
		return;
	const int self = pr_global_struct->self, other = pr_global_struct->other;
	G_INT (OFS_PARM0) = EDICT_TO_PROG (targ);
	G_INT (OFS_PARM1) = EDICT_TO_PROG (inflictor);
	G_INT (OFS_PARM2) = EDICT_TO_PROG (attacker);
	G_FLOAT (OFS_PARM3) = damage;
	pr_global_struct->time = qcvm->time;
	PR_ExecuteProgram (f - qcvm->functions);
	pr_global_struct->self = self;
	pr_global_struct->other = other;
}

// attack_finished is a QuakeC field (defs.qc), not one the engine knows by name
static eval_t *WS_AttackFinished (edict_t *ent)
{
	return ws_attack_finished >= 0 ? GetEdictFieldValue (ent, ws_attack_finished) : NULL;
}

static void WS_SetModel (edict_t *ent, const char *model)
{
	ent->v.model = PR_SetEngineString (model);
	ent->v.modelindex = SV_ModelIndex (model);
}

static void WS_Scale (edict_t *ent, float scale)
{
	eval_t *val = GetEdictFieldValue (ent, qcvm->extfields.scale);
	if (val)
		val->_float = scale;
}

// Throws a few gingerbread shards that the progs' SUB_Remove clears up.
static void WS_Shatter (const float origin[3], const float dir[3])
{
	dfunction_t *remove = ED_FindFunction ("SUB_Remove");
	const char	*model = WS_Model (PLATGUN_SHARD, "progs/gib1.mdl");
	for (int i = 0; i < 6; ++i)
	{
		edict_t *shard = ED_Alloc ();
		shard->v.classname = PR_SetEngineString ("workshop_shard");
		shard->v.movetype = MOVETYPE_BOUNCE;
		shard->v.solid = SOLID_NOT;
		WS_SetModel (shard, model);
		VectorCopy (origin, shard->v.origin);
		for (int a = 0; a < 3; ++a)
			shard->v.velocity[a] = -dir[a] * 120.0f + ((float)(COM_Rand () % 401) - 200.0f);
		shard->v.velocity[2] += 220.0f;
		shard->v.avelocity[0] = (float)(COM_Rand () % 600);
		shard->v.avelocity[1] = (float)(COM_Rand () % 600);
		if (remove)
		{
			shard->v.think = remove - qcvm->functions;
			shard->v.nextthink = qcvm->time + 1.5 + (COM_Rand () % 100) * 0.01;
		}
		SV_LinkEdict (shard, false);
	}
	SV_StartParticle ((float *)origin, vec3_origin, 0x60 + 4, 48); // a puff of crumbs (the palette's browns)
}

/*
==============================================================================

PLATFORM GUN

==============================================================================
*/

static void Platgun_Fire (edict_t *player, int num, ws_player_t *p)
{
	if (ws_num_blobs == MAX_BLOBS)
	{
		if (!ws_blobs[0].ent->free)
			ED_Free (ws_blobs[0].ent);
		memmove (ws_blobs, ws_blobs + 1, (MAX_BLOBS - 1) * sizeof (ws_blobs[0]));
		--ws_num_blobs;
	}
	vec3_t forward, right, up;
	AngleVectors (player->v.v_angle, forward, right, up);
	edict_t *blob = ED_Alloc ();
	blob->v.classname = PR_SetEngineString ("workshop_blob");
	blob->v.movetype = MOVETYPE_NONE; // moved in SV_Workshop_Frame, under half gravity
	blob->v.solid = SOLID_NOT;
	blob->v.owner = EDICT_TO_PROG (player);
	WS_SetModel (blob, WS_Model (PLATGUN_BLOB, "progs/grenade.mdl"));
	WS_Scale (blob, 0.6f);
	for (int a = 0; a < 3; ++a)
	{
		blob->v.origin[a] = player->v.origin[a] + player->v.view_ofs[a] + forward[a] * 14.0f + right[a] * 4.0f - up[a] * 4.0f;
		blob->v.velocity[a] = forward[a] * workshop_platgun_speed.value;
	}
	blob->v.avelocity[0] = 400.0f;
	blob->v.avelocity[2] = 250.0f;
	SV_LinkEdict (blob, false);
	ws_blobs[ws_num_blobs].ent = blob;
	ws_blobs[ws_num_blobs].owner = num;
	ws_blobs[ws_num_blobs].born = qcvm->time;
	++ws_num_blobs;
	SV_StartSound (player, NULL, CHAN_WEAPON, "weapons/grenade.wav", 255, ATTN_NORM);
	--p->clip;
	p->next_fire = qcvm->time + PLATGUN_RATE;
	p->fire_frame_until = qcvm->time + 0.3;
	if (!p->clip && p->reserve)
		p->reload_until = qcvm->time + PLATGUN_RELOAD; // automatic reload
}

// Where a disk goes for a blob that landed at pos on a surface facing normal: flat (walkable),
// half of it out of a wall as a ledge, on a floor lying on it, under a ceiling hanging from it.
static void Platform_Place (const float pos[3], const float normal[3], float center[3])
{
	const float radius = workshop_platform_radius.value, half = workshop_platform_thickness.value * 0.5f;
	const float flat = sqrtf (normal[0] * normal[0] + normal[1] * normal[1]);
	VectorCopy (pos, center);
	if (flat > 0.5f)
	{
		center[0] += normal[0] / flat * radius * 0.45f;
		center[1] += normal[1] / flat * radius * 0.45f;
	}
	if (normal[2] > 0.5f)
		center[2] += half * 0.3f;
	else if (normal[2] < -0.5f)
		center[2] -= half * 0.6f;
}

// A dough blob spreads over the platform delay in three steps, each a logged terrain op.
static void Platform_Grow (const float pos[3], const float normal[3])
{
	if (ws_num_grows == MAX_GROWS)
		return;
	ws_grow_t *g = &ws_grows[ws_num_grows++];
	Platform_Place (pos, normal, g->center);
	g->normal[0] = g->normal[1] = 0.0f;
	g->normal[2] = 1.0f;
	g->start = qcvm->time;
	g->stage = 0;
}

// Anyone the growing disk would bury is lifted onto its top, as a platform in Deep Rock does.
static void Platform_Lift (const float center[3], float radius, float half)
{
	for (int e = 1; e < qcvm->num_edicts; ++e)
	{
		edict_t *ent = EDICT_NUM (e);
		if (ent->free || ent->v.health <= 0 || !((int)ent->v.flags & (FL_CLIENT | FL_MONSTER)) ||
			!Terrain_BoxOverlaps (terrain_active, ent->v.origin, ent->v.mins, ent->v.maxs))
			continue;
		const float dx = ent->v.origin[0] - center[0], dy = ent->v.origin[1] - center[1];
		const float dz = ent->v.origin[2] + ent->v.mins[2] - center[2];
		if (dx * dx + dy * dy > (radius + 24.0f) * (radius + 24.0f) || dz < -half - 64.0f || dz > half + 8.0f)
			continue;
		vec3_t top = {ent->v.origin[0], ent->v.origin[1], center[2] + half - ent->v.mins[2] + 1.0f};
		if (SV_Terrain_BoxInBrushes (top, ent->v.mins, ent->v.maxs))
			continue; // no room on top: leave them where they are
		VectorCopy (top, ent->v.origin);
		SV_LinkEdict (ent, false);
	}
}

static void Platform_GrowFrame (void)
{
	static const float steps[3][2] = {{0.0f, 0.45f}, {PLATFORM_DELAY * 0.5f, 0.75f}, {PLATFORM_DELAY, 1.0f}}; // time, share of the radius
	for (int i = 0; i < ws_num_grows;)
	{
		ws_grow_t *g = &ws_grows[i];
		while (g->stage < 3 && qcvm->time >= g->start + steps[g->stage][0])
		{
			const float share = steps[g->stage][1];
			const float radius = workshop_platform_radius.value * share, half = workshop_platform_thickness.value * 0.5f * (0.6f + 0.4f * share);
			SV_Terrain_Disk (g->center, g->normal, radius, half);
			Platform_Lift (g->center, radius, half);
			++g->stage;
		}
		if (g->stage == 3)
			*g = ws_grows[--ws_num_grows];
		else
			++i;
	}
}

static void Blob_Frame (void)
{
	const float dt = (float)host_frametime;
	for (int i = 0; i < ws_num_blobs;)
	{
		ws_blob_t *b = &ws_blobs[i];
		edict_t	  *blob = b->ent;
		qboolean   done = blob->free || qcvm->time > b->born + PLATGUN_BLOB_LIFE;
		if (!done)
		{
			edict_t *owner = EDICT_NUM (b->owner);
			vec3_t	 end;
			blob->v.velocity[2] -= sv_gravity.value * PLATGUN_GRAVITY * dt;
			VectorMA (blob->v.origin, dt, blob->v.velocity, end);
			trace_t tr = SV_Move (blob->v.origin, vec3_origin, vec3_origin, end, MOVE_MISSILE, owner);
			// the dough swells in flight
			WS_Scale (blob, CLAMP (0.6f, 0.6f + (float)(qcvm->time - b->born) * 2.0f, 1.4f));
			if (tr.fraction < 1.0f || tr.startsolid)
			{
				vec3_t dir;
				VectorCopy (blob->v.velocity, dir);
				VectorNormalize (dir);
				if (tr.ent && tr.ent != qcvm->edicts && tr.ent->v.takedamage)
				{
					// a body: the dough shatters on it
					WS_Shatter (tr.endpos, dir);
					SV_StartSound (blob, NULL, CHAN_AUTO, "zombie/z_gib.wav", 255, ATTN_NORM);
					WS_Damage (tr.ent, blob, owner, workshop_platgun_damage.value);
				}
				else if (!tr.startsolid)
				{
					// anything else: it sticks and spreads into a platform
					SV_StartSound (blob, NULL, CHAN_AUTO, "zombie/z_hit.wav", 255, ATTN_NORM);
					Platform_Grow (tr.endpos, tr.plane.normal);
				}
				done = true;
			}
			else
			{
				VectorCopy (end, blob->v.origin);
				for (int a = 0; a < 3; ++a)
					blob->v.angles[a] = anglemod (blob->v.angles[a] + blob->v.avelocity[a] * dt);
				SV_LinkEdict (blob, false);
			}
		}
		if (done)
		{
			if (!blob->free)
				ED_Free (blob);
			*b = ws_blobs[--ws_num_blobs];
		}
		else
			++i;
	}
}

/*
==============================================================================

GRAPPLE

==============================================================================
*/

static void Grapple_Release (ws_player_t *p)
{
	if (p->hook && !p->hook->free)
		ED_Free (p->hook);
	p->hook = NULL;
	p->hook_state = 0;
}

static void Grapple_Throw (edict_t *player, int num, ws_player_t *p)
{
	vec3_t forward, right, up;
	AngleVectors (player->v.v_angle, forward, right, up);
	edict_t *hook = ED_Alloc ();
	hook->v.classname = PR_SetEngineString ("workshop_hook");
	hook->v.movetype = MOVETYPE_NONE; // moved in SV_Workshop_Frame
	hook->v.solid = SOLID_NOT;
	hook->v.owner = EDICT_TO_PROG (player);
	hook->v.skin = 0;
	hook->v.colormap = num; // which player's light string ends here; the client draws it
	WS_SetModel (hook, WS_HookModel ());
	for (int a = 0; a < 3; ++a)
	{
		hook->v.origin[a] = player->v.origin[a] + player->v.view_ofs[a] + forward[a] * 16.0f;
		hook->v.velocity[a] = forward[a] * workshop_grapple_speed.value;
	}
	VectorAngles (forward, NULL, hook->v.angles);
	SV_LinkEdict (hook, false);
	p->hook = hook;
	p->hook_state = 1;
	SV_StartSound (player, NULL, CHAN_WEAPON, "weapons/ax1.wav", 255, ATTN_NORM);
}

static void Grapple_HookFrame (int num, ws_player_t *p)
{
	edict_t *player = EDICT_NUM (num), *hook = p->hook;
	if (!hook || hook->free)
	{
		p->hook = NULL;
		p->hook_state = 0;
		return;
	}
	if (p->hook_state != 1)
		return;
	const float dt = (float)host_frametime;
	vec3_t		end, from_player;
	VectorMA (hook->v.origin, dt, hook->v.velocity, end);
	trace_t tr = SV_Move (hook->v.origin, vec3_origin, vec3_origin, end, MOVE_MISSILE, player);
	VectorSubtract (tr.endpos, player->v.origin, from_player);
	if (tr.fraction < 1.0f || tr.startsolid)
	{
		if (tr.ent && tr.ent != qcvm->edicts && tr.ent->v.takedamage)
		{
			WS_Damage (tr.ent, hook, player, 10.0f);
			SV_StartSound (hook, NULL, CHAN_AUTO, "weapons/tink1.wav", 255, ATTN_NORM);
			Grapple_Release (p);
			return;
		}
		VectorCopy (tr.endpos, hook->v.origin);
		VectorCopy (tr.endpos, p->anchor);
		p->hook_state = 2;
		SV_LinkEdict (hook, false);
		SV_StartSound (hook, NULL, CHAN_AUTO, "weapons/tink1.wav", 255, ATTN_NORM);
		return;
	}
	if (DotProduct (from_player, from_player) > workshop_grapple_range.value * workshop_grapple_range.value)
	{
		Grapple_Release (p);
		return;
	}
	VectorCopy (end, hook->v.origin);
	SV_LinkEdict (hook, false);
}

// While the hook holds, the line pulls the player toward it; near it, the player hangs there.
static void Grapple_Pull (edict_t *player, ws_player_t *p)
{
	vec3_t eye, to;
	VectorAdd (player->v.origin, player->v.view_ofs, eye);
	VectorSubtract (p->anchor, eye, to);
	const float dist = VectorNormalize (to);
	player->v.flags = (int)player->v.flags & ~FL_ONGROUND;
	if (dist > 48.0f)
		VectorScale (to, workshop_grapple_pull.value, player->v.velocity);
	else
		VectorScale (to, dist * 4.0f, player->v.velocity); // hang at the hook
	// the server's own player move cancels gravity for this frame against the pull
	player->v.velocity[2] += sv_gravity.value * (float)host_frametime;
}

/*
==============================================================================

PER PLAYER

==============================================================================
*/

static void WS_SetTool (edict_t *player, ws_player_t *p, int tool)
{
	if (p->tool == tool)
		return;
	if (p->tool == TOOL_GRAPPLE)
		Grapple_Release (p);
	p->tool = tool;
	// putting a tool away only stops holding the progs' weapon back (never shortens its own cooldown)
	if (tool != TOOL_NONE)
		SV_StartSound (player, NULL, CHAN_ITEM, "weapons/lock4.wav", 160, ATTN_NORM);
}

// Once a server frame per player, before its commands run: things that follow the clock.
void SV_Workshop_PlayerFrame (edict_t *ent, int num)
{
	if (!WS_Active () || num < 1 || num > MAX_SCOREBOARD)
		return;
	ws_player_t *p = &ws_players[num];
	client_t	*client = &svs.clients[num - 1];

	// flares on the wrist come back over time, and the client is told how many it carries
	if (p->flares < (int)workshop_flares.value && workshop_flare_regen.value > 0)
	{
		if (!p->flare_regen_at)
			p->flare_regen_at = qcvm->time + workshop_flare_regen.value;
		else if (qcvm->time >= p->flare_regen_at)
		{
			++p->flares;
			p->flare_regen_at = 0;
		}
	}
	if (p->flares != p->flares_sent && client->spawned && client->protocol_qsvr == QSVR_PROTOCOL_PINNED && !client->message.overflowed &&
		client->message.cursize + 64 < client->message.maxsize)
	{
		MSG_WriteByte (&client->message, svc_stufftext);
		MSG_WriteString (&client->message, va ("//workshop_flares %d\n", p->flares));
		p->flares_sent = p->flares;
	}

	// platform reserve comes back slowly, the way resupply would
	if (p->clip + p->reserve < PLATGUN_MAX && workshop_platgun_regen.value > 0)
	{
		if (!p->regen_at)
			p->regen_at = qcvm->time + workshop_platgun_regen.value;
		else if (qcvm->time >= p->regen_at)
		{
			++p->reserve;
			p->regen_at = 0;
		}
	}
	if (p->reload_until && qcvm->time >= p->reload_until)
	{
		const int take = q_min (PLATGUN_CLIP - p->clip, p->reserve);
		p->clip += take;
		p->reserve -= take;
		p->reload_until = 0;
	}
	else if (!p->reload_until && !p->clip && p->reserve)
		p->reload_until = qcvm->time + PLATGUN_RELOAD;

	if (ent->free || ent->v.health <= 0)
		WS_SetTool (ent, p, TOOL_NONE);
	else if (p->hook_state == 2)
		Grapple_Pull (ent, p);
}

/*
Right before PlayerPreThink, for each command the player sends: the command's impulse and buttons
are on the edict here. A tool takes its impulse before the progs see it, and while a tool is out
the progs' own weapon is held back (attack_finished stays ahead of time).
*/
void SV_Workshop_PreThink (edict_t *ent)
{
	const int num = NUM_FOR_EDICT (ent);
	if (!WS_Active () || num < 1 || num > MAX_SCOREBOARD || ent->free || ent->v.health <= 0)
		return;
	ws_player_t *p = &ws_players[num];

	// tool selection: impulses the progs do not use, and any ordinary weapon impulse puts it away
	const int impulse = (int)ent->v.impulse;
	if (impulse == IMPULSE_PLATGUN || impulse == IMPULSE_GRAPPLE)
	{
		const int want = impulse == IMPULSE_PLATGUN ? TOOL_PLATGUN : TOOL_GRAPPLE;
		WS_SetTool (ent, p, p->tool == want ? TOOL_NONE : want);
		ent->v.impulse = 0;
	}
	else if (impulse && p->tool)
		WS_SetTool (ent, p, TOOL_NONE); // any of the progs' own impulses: they act on it themselves
	if (p->tool == TOOL_NONE)
		return;

	eval_t *finished = WS_AttackFinished (ent);
	if (finished && finished->_float < qcvm->time + 0.1)
		finished->_float = qcvm->time + 0.1;
	const qboolean attack = ent->v.button0 != 0;
	if (p->tool == TOOL_PLATGUN)
	{
		if (attack && p->clip > 0 && !p->reload_until && qcvm->time >= p->next_fire)
			Platgun_Fire (ent, num, p);
	}
	else if (p->tool == TOOL_GRAPPLE)
	{
		if (attack && !p->hook_state)
			Grapple_Throw (ent, num, p);
		else if (!attack && p->hook_state)
			Grapple_Release (p);
		if (p->hook_state == 2)
			Grapple_Pull (ent, p);
	}
}

// release_hook: the slot's hook is still this map's edict (not on a map change, where it may be reused)
static void WS_ResetSlot (ws_player_t *p, qboolean release_hook)
{
	if (release_hook)
		Grapple_Release (p);
	memset (p, 0, sizeof (*p));
	p->clip = PLATGUN_CLIP;
	p->reserve = PLATGUN_MAX - PLATGUN_CLIP;
	p->flares = (int)workshop_flares.value;
	p->flares_sent = -1;
}

void SV_Workshop_Frame (void)
{
	if (!WS_Active ())
		return;
	if (ws_sweep)
	{
		// a saved game brings back hooks and blobs nothing here holds any more
		ws_sweep = false;
		for (int e = svs.maxclients + 1; e < qcvm->num_edicts; ++e)
		{
			edict_t *ent = EDICT_NUM (e);
			if (!ent->free && (!strcmp (PR_GetString (ent->v.classname), "workshop_hook") || !strcmp (PR_GetString (ent->v.classname), "workshop_blob")))
				ED_Free (ent);
		}
	}
	// a slot whose player left starts fresh for the next one (and its hook goes)
	for (int num = 1; num <= svs.maxclients && num <= MAX_SCOREBOARD; ++num)
	{
		const client_t *client = &svs.clients[num - 1];
		ws_player_t	   *p = &ws_players[num];
		if ((!client->active || !client->spawned) && (p->tool || p->hook || p->flares_sent != -1))
			WS_ResetSlot (p, true);
	}
	Blob_Frame ();
	Platform_GrowFrame ();
	for (int num = 1; num <= svs.maxclients && num <= MAX_SCOREBOARD; ++num)
		if (ws_players[num].hook_state)
			Grapple_HookFrame (num, &ws_players[num]);
}

int SV_Workshop_ViewModel (client_t *client, int *modelindex, float *frame, float *ammo)
{
	const int num = (int)(client - svs.clients) + 1;
	if (!WS_Active () || num < 1 || num > MAX_SCOREBOARD)
		return 0;
	const ws_player_t *p = &ws_players[num];
	if (p->tool == TOOL_PLATGUN)
	{
		*modelindex = SV_ModelIndex (WS_Model (PLATGUN_VIEWMODEL, "progs/v_rock.mdl"));
		*frame = qcvm->time < p->fire_frame_until ? (qcvm->time < p->fire_frame_until - 0.2 ? 1.0f : 2.0f) : 0.0f;
		*ammo = (float)(p->clip + p->reserve);
		return 1;
	}
	if (p->tool == TOOL_GRAPPLE)
	{
		*modelindex = SV_ModelIndex (WS_GrappleViewModel ());
		*frame = p->hook_state ? 1.0f : 0.0f;
		*ammo = 0.0f;
		return 1;
	}
	return 0;
}

/*
workshop_tool <platgun|grapple|off>
Takes a tool out or puts it away; a VR hand reaching for its holster sends this.
*/
static void SV_Workshop_Tool_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}
	if (!WS_Active () || !host_client || !host_client->edict)
		return;
	const int	num = NUM_FOR_EDICT (host_client->edict);
	const char *which = Cmd_Argv (1);
	if (num < 1 || num > MAX_SCOREBOARD)
		return;
	WS_SetTool (host_client->edict, &ws_players[num], !strcmp (which, "platgun") ? TOOL_PLATGUN : !strcmp (which, "grapple") ? TOOL_GRAPPLE : TOOL_NONE);
}

/*
workshop_throwflare <origin x y z> <velocity x y z>
A VR hand let go of a flare it took off the wrist. The server checks the throw is near the player
and not absurdly fast, and that a flare is left.
*/
static void SV_Workshop_ThrowFlare_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}
	if (!WS_Active () || !host_client || !host_client->edict || Cmd_Argc () != 7)
		return;
	edict_t	 *player = host_client->edict;
	const int num = NUM_FOR_EDICT (player);
	if (num < 1 || num > MAX_SCOREBOARD || player->v.health <= 0 || ws_players[num].flares <= 0)
		return;
	vec3_t origin, velocity, eye, gap;
	for (int a = 0; a < 3; ++a)
	{
		origin[a] = (float)atof (Cmd_Argv (1 + a));
		velocity[a] = (float)atof (Cmd_Argv (4 + a));
		if (!isfinite (origin[a]) || !isfinite (velocity[a]))
			return;
	}
	VectorAdd (player->v.origin, player->v.view_ofs, eye);
	VectorSubtract (origin, eye, gap);
	if (DotProduct (gap, gap) > 96.0f * 96.0f)
		VectorCopy (eye, origin); // a hand cannot be that far away
	const float speed = sqrtf (DotProduct (velocity, velocity));
	if (speed > 1200.0f)
		VectorScale (velocity, 1200.0f / speed, velocity);
	--ws_players[num].flares;
	SV_Terrain_ThrowFlare (player, origin, velocity);
}

void SV_Workshop_Init (void)
{
	Cvar_RegisterVariable (&workshop_tools);
	Cvar_RegisterVariable (&workshop_platform_radius);
	Cvar_RegisterVariable (&workshop_platform_thickness);
	Cvar_RegisterVariable (&workshop_platgun_speed);
	Cvar_RegisterVariable (&workshop_platgun_damage);
	Cvar_RegisterVariable (&workshop_platgun_regen);
	Cvar_RegisterVariable (&workshop_grapple_speed);
	Cvar_RegisterVariable (&workshop_grapple_pull);
	Cvar_RegisterVariable (&workshop_grapple_range);
	Cvar_RegisterVariable (&workshop_flares);
	Cvar_RegisterVariable (&workshop_flare_regen);
	Cmd_AddCommand_ClientCommand ("workshop_tool", SV_Workshop_Tool_f);
	Cmd_AddCommand_ClientCommand ("workshop_throwflare", SV_Workshop_ThrowFlare_f);
}
