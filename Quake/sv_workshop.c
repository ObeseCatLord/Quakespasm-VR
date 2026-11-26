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
 * Lava sprayer ("molten candy"): a red-and-white striped hose gun with a glass tank of glowing lava.
 * Holding fire sprays the particle lava of sv_terrain_lava.c from the player's view (the weapon hand
 * in VR) through that file's emitter for the player, so it works for any client. The tank (shown
 * as the ammo count, in percent) drains while the nozzle is open, and refills slowly once it has
 * been shut a moment; once run dry it waits until the tank is partly full again.
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
#define LAVAGUN_VIEWMODEL "progs/workshop/v_lavagun.mdl"

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
	TOOL_GRAPPLE,
	TOOL_LAVAGUN
};

#define IMPULSE_PLATGUN 70
#define IMPULSE_GRAPPLE 71
#define IMPULSE_LAVAGUN 72
#define IMPULSE_NEXT	10 // Quake's next and previous weapon (the mouse wheel)
#define IMPULSE_PREV	12

/*
The tools in wheel order. Each has its own impulse (a key) that takes it out or puts it away. The
next-weapon impulse goes from Quake's last usable weapon to the first tool here, through the rest
in order, then back to Quake's first weapon; previous-weapon goes the other way. A new tool is one
more line here (plus its TOOL_ value, its fire code in SV_Workshop_PreThink and its view model);
its key bind is in cl_workshop.c.
*/
static const struct
{
	int			tool, impulse;
	const char *name;
} ws_tools[] = {
	{TOOL_GRAPPLE, IMPULSE_GRAPPLE, "grapple"},
	{TOOL_PLATGUN, IMPULSE_PLATGUN, "platgun"},
	{TOOL_LAVAGUN, IMPULSE_LAVAGUN, "lavagun"},
};
#define NUM_WS_TOOLS ((int)(sizeof (ws_tools) / sizeof (ws_tools[0])))

// Quake's own weapons in its cycle order (weapons.qc CycleWeaponCommand), with the impulse that selects each
static const struct
{
	int item, impulse;
} ws_stock[] = {
	{IT_AXE, 1},		   {IT_SHOTGUN, 2},			 {IT_SUPER_SHOTGUN, 3},	  {IT_NAILGUN, 4},
	{IT_SUPER_NAILGUN, 5}, {IT_GRENADE_LAUNCHER, 6}, {IT_ROCKET_LAUNCHER, 7}, {IT_LIGHTNING, 8},
};
#define NUM_WS_STOCK ((int)(sizeof (ws_stock) / sizeof (ws_stock[0])))
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
static cvar_t workshop_lava_drain = {"workshop_lava_drain", "12", CVAR_NONE};	   // tank percent used per second of spraying
static cvar_t workshop_lava_refill = {"workshop_lava_refill", "5", CVAR_NONE};	   // tank percent restored per second once the nozzle is shut; 0 never
static cvar_t workshop_lava_delay = {"workshop_lava_delay", "1.0", CVAR_NONE};	   // seconds the nozzle stays shut before the tank starts to refill
static cvar_t workshop_lava_restart = {"workshop_lava_restart", "20", CVAR_NONE};  // tank percent needed to spray again after running dry
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
	qboolean holding;		 // attack_finished is being held ahead of time for a tool
	float	 progs_finished; // the progs' own attack_finished when the hold began
	float	 lava_tank;		 // lava sprayer tank, percent
	double	 lava_last;		 // when the tank was last drained
	double	 lava_until;	 // the nozzle counts as open until then (a command can lag a frame)
	qboolean lava_dry;		 // ran empty: no spray until the tank is back to workshop_lava_restart
	qboolean lava_snd;		 // the spray's sound is playing
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

void SV_Workshop_LoadingSave (void)
{
	// the edicts about to be read replace everything held here (a fast load keeps the map, so the
	// pointers would otherwise go on pointing at whatever the save puts in those slots); hooks and
	// blobs the save brings back are swept once the game runs again
	for (int i = 1; i <= MAX_SCOREBOARD; ++i)
		WS_ResetSlot (&ws_players[i], false);
	ws_num_blobs = ws_num_grows = 0;
	ws_sweep = true;
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
	SV_Precache_Model (WS_Model (LAVAGUN_VIEWMODEL, "progs/v_rock.mdl"));
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
	SV_Precache_Sound ("ambience/fire1.wav");
	SV_Precache_Sound ("misc/null.wav");
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
	if (hook && !hook->free && (player->free || player->v.health <= 0))
		Grapple_Release (p); // its thrower died: the hook goes with them
	hook = p->hook;
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
	if (p->tool == TOOL_LAVAGUN)
		p->lava_until = 0.0; // the nozzle shuts; its emitter lapses by itself
	p->tool = tool;
	// putting a tool away gives the progs back their own cooldown, so a weapon impulse in the same
	// command (a key, or the wheel going past the last tool) is not dropped by W_WeaponFrame
	if (tool == TOOL_NONE && p->holding)
	{
		eval_t *finished = player->free ? NULL : WS_AttackFinished (player);
		if (finished)
			finished->_float = p->progs_finished;
		p->holding = false;
	}
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
		MSG_WriteString (&client->message, va ("//workshop_flarecount %d\n", p->flares));
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

	// lava tank: refills once the nozzle has been shut a moment, and the spray's sound stops with it
	const qboolean spraying = qcvm->time < p->lava_until;
	if (!spraying && p->lava_snd && !ent->free)
	{
		SV_StartSound (ent, NULL, CHAN_WEAPON, "misc/null.wav", 255, ATTN_NORM);
		p->lava_snd = false;
	}
	if (!spraying && p->lava_tank < 100.0f && workshop_lava_refill.value > 0 && qcvm->time >= p->lava_until + workshop_lava_delay.value)
	{
		p->lava_tank = q_min (100.0f, p->lava_tank + workshop_lava_refill.value * (float)host_frametime);
		if (p->lava_dry && p->lava_tank >= workshop_lava_restart.value)
			p->lava_dry = false;
	}

	if (ent->free || ent->v.health <= 0)
		WS_SetTool (ent, p, TOOL_NONE);
	else if (p->hook_state == 2)
		Grapple_Pull (ent, p);
}

static void WS_Wheel (edict_t *ent, ws_player_t *p, int dir);

/*
==============================================================================

LAVA SPRAYER

==============================================================================
*/

// Holding fire: one call per command. In VR the nozzle follows the weapon hand (the command carries
// its pose relative to the body); otherwise it comes from the view.
static void Lavagun_Spray (edict_t *player, int num, ws_player_t *p)
{
	if (p->lava_dry || p->lava_tank <= 0.0f)
	{
		if (!p->lava_dry)
		{
			p->lava_dry = true;
			SV_ClientPrintf ("Lava tank empty\n");
		}
		return;
	}
	const usercmd_t *cmd = &svs.clients[num - 1].cmd;
	vec3_t			 angles, forward, right, up, eye;
	VectorCopy (player->v.v_angle, angles);
	VectorAdd (player->v.origin, player->v.view_ofs, eye);
	if (cmd->vr_active && cmd->vr_handpos_relative && isfinite (cmd->vr_handpos[0]) && isfinite (cmd->vr_handpos[1]) && isfinite (cmd->vr_handpos[2]) &&
		isfinite (cmd->vr_handrot[0]) && isfinite (cmd->vr_handrot[1]) && isfinite (cmd->vr_handrot[2]) && VectorLength (cmd->vr_handpos) <= 96.0f)
	{
		VectorAdd (player->v.origin, cmd->vr_handpos, eye);
		VectorCopy (cmd->vr_handrot, angles);
	}
	AngleVectors (angles, forward, right, up);
	if (!SV_Terrain_LavaSpray (num, eye, forward, up, player->v.velocity))
		return; // nozzle against rock: nothing comes out, and nothing is used
	const double dt = (qcvm->time - p->lava_last) < 0.1 ? qcvm->time - p->lava_last : 0.0;
	p->lava_last = qcvm->time;
	p->lava_until = qcvm->time + 0.15;
	p->lava_tank -= workshop_lava_drain.value * (float)dt;
	if (p->lava_tank <= 0.0f)
	{
		p->lava_tank = 0.0f;
		p->lava_dry = true;
		SV_ClientPrintf ("Lava tank empty\n");
	}
	if (!p->lava_snd)
	{
		SV_StartSound (player, NULL, CHAN_WEAPON, "ambience/fire1.wav", 200, ATTN_NORM);
		p->lava_snd = true;
	}
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
	int		  key = -1;
	for (int i = 0; i < NUM_WS_TOOLS; ++i)
		if (impulse == ws_tools[i].impulse)
			key = i;
	if (key >= 0)
	{
		WS_SetTool (ent, p, p->tool == ws_tools[key].tool ? TOOL_NONE : ws_tools[key].tool);
		ent->v.impulse = 0;
	}
	else if (impulse == IMPULSE_NEXT || impulse == IMPULSE_PREV)
		WS_Wheel (ent, p, impulse == IMPULSE_NEXT ? 1 : -1);
	else if (impulse && p->tool)
		WS_SetTool (ent, p, TOOL_NONE); // any of the progs' own impulses: they act on it themselves
	if (p->tool == TOOL_NONE)
		return;

	eval_t *finished = WS_AttackFinished (ent);
	if (finished && !p->holding)
	{
		p->progs_finished = finished->_float;
		p->holding = true;
	}
	if (finished && finished->_float < qcvm->time + 0.1)
		finished->_float = qcvm->time + 0.1;
	const qboolean attack = ent->v.button0 != 0;
	if (p->tool == TOOL_PLATGUN)
	{
		if (attack && p->clip > 0 && !p->reload_until && qcvm->time >= p->next_fire)
			Platgun_Fire (ent, num, p);
	}
	else if (p->tool == TOOL_LAVAGUN)
	{
		if (attack)
			Lavagun_Spray (ent, num, p);
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

// The tool's place in ws_tools, or -1 (no tool out)
static int WS_ToolSlot (int tool)
{
	for (int i = 0; i < NUM_WS_TOOLS; ++i)
		if (ws_tools[i].tool == tool)
			return i;
	return -1;
}

// Whether the progs' cycle would stop on this weapon: held, with the ammo the cycle asks for
static qboolean WS_StockUsable (const edict_t *ent, int i)
{
	if (!((int)ent->v.items & ws_stock[i].item))
		return false;
	switch (ws_stock[i].item)
	{
	case IT_SHOTGUN:
		return ent->v.ammo_shells >= 1;
	case IT_SUPER_SHOTGUN:
		return ent->v.ammo_shells >= 2;
	case IT_NAILGUN:
		return ent->v.ammo_nails >= 1;
	case IT_SUPER_NAILGUN:
		return ent->v.ammo_nails >= 2;
	case IT_GRENADE_LAUNCHER:
	case IT_ROCKET_LAUNCHER:
		return ent->v.ammo_rockets >= 1;
	case IT_LIGHTNING:
		return ent->v.ammo_cells >= 1;
	default:
		return true;
	}
}

// The impulse for Quake's first (dir 1) or last (dir -1) usable weapon, 0 when there is none
static int WS_StockEnd (const edict_t *ent, int dir)
{
	for (int k = 0; k < NUM_WS_STOCK; ++k)
	{
		const int i = dir > 0 ? k : NUM_WS_STOCK - 1 - k;
		if (WS_StockUsable (ent, i))
			return ws_stock[i].impulse;
	}
	return 0;
}

// Whether the progs' cycle from the current weapon would wrap round its end (dir 1: past the last weapon)
static qboolean WS_StockWraps (const edict_t *ent, int dir)
{
	int at = -1;
	for (int i = 0; i < NUM_WS_STOCK; ++i)
		if ((int)ent->v.weapon == ws_stock[i].item)
			at = i;
	if (at < 0)
		return false; // a weapon this table does not know: leave it to the progs
	for (int i = at + dir; i >= 0 && i < NUM_WS_STOCK; i += dir)
		if (WS_StockUsable (ent, i))
			return false;
	return true;
}

// Next or previous weapon with the tools on the wheel after Quake's own weapons
static void WS_Wheel (edict_t *ent, ws_player_t *p, int dir)
{
	const int at = WS_ToolSlot (p->tool);
	if (at >= 0)
	{
		const int next = at + dir;
		if (next >= 0 && next < NUM_WS_TOOLS)
		{
			WS_SetTool (ent, p, ws_tools[next].tool);
			ent->v.impulse = 0;
		}
		else
		{
			// off the end of the tools: the progs select Quake's first or last weapon themselves
			WS_SetTool (ent, p, TOOL_NONE);
			ent->v.impulse = WS_StockEnd (ent, dir);
		}
		return;
	}
	if (WS_StockWraps (ent, dir))
	{
		WS_SetTool (ent, p, ws_tools[dir > 0 ? 0 : NUM_WS_TOOLS - 1].tool);
		ent->v.impulse = 0;
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
	p->lava_tank = 100.0f;
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
	if (p->tool == TOOL_LAVAGUN)
	{
		*modelindex = SV_ModelIndex (WS_Model (LAVAGUN_VIEWMODEL, "progs/v_rock.mdl"));
		*frame = qcvm->time < p->lava_until ? (((int)(qcvm->time * 24.0)) & 1 ? 1.0f : 2.0f) : 0.0f; // the gun shudders while it sprays
		*ammo = ceilf (p->lava_tank);
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
workshop_tool <platgun|grapple|lavagun|off>  (any name in ws_tools)
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
	int			tool = TOOL_NONE;
	for (int i = 0; i < NUM_WS_TOOLS; ++i)
		if (!strcmp (which, ws_tools[i].name))
			tool = ws_tools[i].tool;
	WS_SetTool (host_client->edict, &ws_players[num], tool);
}

/*
workshop_info
Per player on this server: the tool out (or "none"), the progs' current weapon, the platform gun's
clip and reserve, and the wrist's flares. The tests read it.
*/
static void SV_Workshop_Info_f (void)
{
	if (!sv.active || !WS_Active ())
	{
		Con_Printf ("workshop_info: no workshop map running\n");
		return;
	}
	PR_SwitchQCVM (&sv.qcvm);
	for (int num = 1; num <= svs.maxclients && num <= MAX_SCOREBOARD; ++num)
	{
		const client_t	  *client = &svs.clients[num - 1];
		const ws_player_t *p = &ws_players[num];
		if (!client->active || !client->spawned)
			continue;
		const int at = WS_ToolSlot (p->tool);
		Con_Printf (
			"workshop_info: player %d tool %s weapon %d clip %d reserve %d flares %d lava %.1f%s shells %d\n", num, at >= 0 ? ws_tools[at].name : "none",
			(int)client->edict->v.weapon, p->clip, p->reserve, p->flares, p->lava_tank, p->lava_dry ? " dry" : "", (int)client->edict->v.ammo_shells);
	}
	PR_SwitchQCVM (NULL);
}

/*
workshop_flare
The flare button: throws one from the view, the way terrain_flare does, from the same flares a VR
wrist carries (they come back over time).
*/
static void SV_Workshop_Flare_f (void)
{
	if (cmd_source == src_command)
	{
		Cmd_ForwardToServer ();
		return;
	}
	if (!WS_Active () || !host_client || !host_client->edict)
		return;
	edict_t	 *player = host_client->edict;
	const int num = NUM_FOR_EDICT (player);
	if (num < 1 || num > MAX_SCOREBOARD || player->v.health <= 0)
		return;
	if (ws_players[num].flares <= 0)
	{
		SV_ClientPrintf ("No flares left\n");
		return;
	}
	vec3_t forward, right, up, origin, velocity;
	AngleVectors (player->v.v_angle, forward, right, up);
	for (int a = 0; a < 3; ++a)
	{
		origin[a] = player->v.origin[a] + player->v.view_ofs[a] + forward[a] * 12.0f;
		velocity[a] = forward[a] * 650.0f + up[a] * 120.0f + player->v.velocity[a] * 0.5f;
	}
	if (SV_Terrain_ThrowFlare (player, origin, velocity))
		--ws_players[num].flares;
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
	if (SV_Terrain_ThrowFlare (player, origin, velocity))
		--ws_players[num].flares;
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
	Cvar_RegisterVariable (&workshop_lava_drain);
	Cvar_RegisterVariable (&workshop_lava_refill);
	Cvar_RegisterVariable (&workshop_lava_delay);
	Cvar_RegisterVariable (&workshop_lava_restart);
	Cvar_RegisterVariable (&workshop_flares);
	Cvar_RegisterVariable (&workshop_flare_regen);
	Cmd_AddCommand_ClientCommand ("workshop_tool", SV_Workshop_Tool_f);
	Cmd_AddCommand_ClientCommand ("workshop_throwflare", SV_Workshop_ThrowFlare_f);
	Cmd_AddCommand_ClientCommand ("workshop_flare", SV_Workshop_Flare_f);
	Cmd_AddCommand ("workshop_info", SV_Workshop_Info_f);
}
