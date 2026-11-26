/*
 * A greybox segmented ice worm for the editable terrain prototype.
 * GPL-2.0-or-later, as the rest of the engine.
 *
 * The head burrows through rock (digging a tunnel with SV_Terrain_Edit as it goes), now and then
 * hunts the player: it picks a point on the cave surface, warns there, bursts out and flies a
 * ballistic arc over the player, then dives back into rock. Body segments follow the head's
 * recorded path. Every segment is an ordinary server edict (SOLID_BBOX, takedamage), positioned
 * here each frame; no QuakeC changes are needed.
 */
#include "quakedef.h"
#include "terrain.h"

#define WORM_MAX_SEG  14
#define WORM_PATH	  512
#define WORM_PATH_GAP 8.0f
#define WORM_MAX_DIGS 512

#define HEAD_MODEL	 "progs/h_shams.mdl"
#define BODY_MODEL	 "progs/lavaball.mdl"
#define WARN_MODEL	 "progs/s_light.spr"
#define SND_WARN	 "ambience/thunder1.wav"
#define SND_BURST	 "boss1/out1.wav"
#define SND_SEGDIE	 "zombie/z_gib.wav"
#define SND_WORMDIE	 "boss1/death.wav"

static cvar_t terrain_worm = {"terrain_worm", "0", CVAR_NONE};					  // 1: one worm at map load
static cvar_t terrain_worm_auto = {"terrain_worm_auto", "1", CVAR_NONE};			  // 0: hunts only on terrain_worm_hunt
static cvar_t terrain_worm_speed = {"terrain_worm_speed", "220", CVAR_NONE};		  // cruising speed through rock
static cvar_t terrain_worm_hunt_speed = {"terrain_worm_hunt_speed", "320", CVAR_NONE};
static cvar_t terrain_worm_turn = {"terrain_worm_turn", "60", CVAR_NONE};			  // deg/s cruising
static cvar_t terrain_worm_hunt_turn = {"terrain_worm_hunt_turn", "90", CVAR_NONE};
static cvar_t terrain_worm_dig_radius = {"terrain_worm_dig_radius", "44", CVAR_NONE};
static cvar_t terrain_worm_dig_step = {"terrain_worm_dig_step", "40", CVAR_NONE};
static cvar_t terrain_worm_max_ops = {"terrain_worm_max_ops", "400", CVAR_NONE};
static cvar_t terrain_worm_launch = {"terrain_worm_launch", "520", CVAR_NONE};		  // least speed leaving the rock on a hunt
static cvar_t terrain_worm_gravity = {"terrain_worm_gravity", "800", CVAR_NONE};
static cvar_t terrain_worm_telegraph = {"terrain_worm_telegraph", "1.5", CVAR_NONE};  // seconds of warning before the burst
static cvar_t terrain_worm_hunt_min = {"terrain_worm_hunt_min", "6", CVAR_NONE};
static cvar_t terrain_worm_hunt_max = {"terrain_worm_hunt_max", "10", CVAR_NONE};
static cvar_t terrain_worm_damage = {"terrain_worm_damage", "25", CVAR_NONE};
static cvar_t terrain_worm_aim_height = {"terrain_worm_aim_height", "96", CVAR_NONE}; // apex above the player's head
static cvar_t terrain_worm_segments = {"terrain_worm_segments", "14", CVAR_NONE};
static cvar_t terrain_worm_freeze = {"terrain_worm_freeze", "0", CVAR_NONE};		  // debug: hang in the air so a weapon test has a still target
static cvar_t terrain_worm_spacing = {"terrain_worm_spacing", "28", CVAR_NONE};

enum { WS_BURROW, WS_AIR };
enum { SEG_HEAD, SEG_BODY, SEG_TAIL };

typedef struct
{
	edict_t *ent;
	int		 kind;
	float	 radius, last_health, flash_until;
} wseg_t;

static struct
{
	int	  active, state;
	float head[3], vel[3], dir[3], speed;
	float path[WORM_PATH][3], plen[WORM_PATH];
	int	  pn;
	float spacing;
	wseg_t seg[WORM_MAX_SEG];
	int	   nseg;
	// digging
	float last_dig[3];
	float air_run;
	int	  ops, dig_attempts;
	float digs[WORM_MAX_DIGS][3];
	int	  num_digs;
	float first_dig[3], first_before;
	// hunting
	struct
	{
		int	   active, phase, warned;
		float  E[3], S[3], T[3], v[3], n[3];
		double deadline, next_snd;
		edict_t *warn;
		double warn_off;
	} hunt;
	double next_hunt, last_hit;
	float  theta;
	// stats
	int	  air_field, air_run_n, air_hunt;
	int	  hunts, bursts, hits, kills_segments;
	float max_err_since;
	uint32_t rng;
} worm;

static float WRand (void)
{
	worm.rng ^= worm.rng << 13;
	worm.rng ^= worm.rng >> 17;
	worm.rng ^= worm.rng << 5;
	return (worm.rng & 0xffffff) / (float)0x1000000;
}

static float Dist (const float a[3], const float b[3])
{
	vec3_t d;
	VectorSubtract (a, b, d);
	return sqrtf (DotProduct (d, d));
}

static void FreeWarn (void)
{
	if (worm.hunt.warn && !worm.hunt.warn->free)
		ED_Free (worm.hunt.warn);
	worm.hunt.warn = NULL;
}

void SV_Terrain_WormReset (void)
{
	memset (&worm, 0, sizeof (worm)); // edicts are gone with the map; nothing to free
	worm.rng = 2463534242u;
}

static void WormKill (const char *why)
{
	for (int i = 0; i < worm.nseg; ++i)
		if (worm.seg[i].ent && !worm.seg[i].ent->free)
		{
			if (i == 0)
				SV_StartSound (worm.seg[i].ent, worm.seg[i].ent->v.origin, 1, SND_WORMDIE, 255, 0.6f);
			ED_Free (worm.seg[i].ent);
		}
	FreeWarn ();
	Con_Printf ("terrain_worm: died (%s), %d hunts, %d bursts, %d player hits\n", why, worm.hunts, worm.bursts, worm.hits);
	const int hunts = worm.hunts, bursts = worm.bursts, hits = worm.hits, ops = worm.ops;
	worm.nseg = 0;
	worm.active = 0;
	worm.hunts = hunts; worm.bursts = bursts; worm.hits = hits; worm.ops = ops; // keep the stats for the info command
}

// Position on the head's path `s` units of arc length behind the head.
static void PathAt (float s, float out[3])
{
	const float head_len = worm.plen[(worm.pn - 1) % WORM_PATH] + Dist (worm.head, worm.path[(worm.pn - 1) % WORM_PATH]);
	float		a = head_len - s;
	if (a < 0)
		a = 0;
	int j = worm.pn - 1;
	while (j > 0 && j > worm.pn - WORM_PATH + 1 && worm.plen[j % WORM_PATH] > a)
		--j;
	const float *p0 = worm.path[j % WORM_PATH];
	const float *p1 = j == worm.pn - 1 ? worm.head : worm.path[(j + 1) % WORM_PATH];
	const float l0 = worm.plen[j % WORM_PATH];
	const float l1 = j == worm.pn - 1 ? head_len : worm.plen[(j + 1) % WORM_PATH];
	const float f = l1 > l0 + 1e-4f ? CLAMP (0.0f, (a - l0) / (l1 - l0), 1.0f) : 0.0f;
	for (int i = 0; i < 3; ++i)
		out[i] = p0[i] + (p1[i] - p0[i]) * f;
}

static void PushPath (void)
{
	const int	 last = (worm.pn - 1) % WORM_PATH;
	const float d = Dist (worm.head, worm.path[last]);
	if (d < WORM_PATH_GAP)
		return;
	const int k = worm.pn % WORM_PATH;
	VectorCopy (worm.head, worm.path[k]);
	worm.plen[k] = worm.plen[last] + d;
	++worm.pn;
}

static void TerrainBounds (float lo[3], float hi[3], float margin)
{
	const terrain_t *t = terrain_active;
	for (int i = 0; i < 3; ++i)
	{
		lo[i] = (float)t->origin[i] + margin;
		hi[i] = (float)t->origin[i] + (float)(t->size[i] * TERRAIN_CHUNK_UNITS) - margin;
	}
}

static void Gradient (const float p[3], float g[3])
{
	const float h = 6.0f;
	for (int a = 0; a < 3; ++a)
	{
		float lo[3], hi[3];
		VectorCopy (p, lo);
		VectorCopy (p, hi);
		lo[a] -= h;
		hi[a] += h;
		g[a] = Terrain_SampleWorld (terrain_active, hi) - Terrain_SampleWorld (terrain_active, lo);
	}
	VectorNormalize (g);
}

static edict_t *SpawnEdict (const char *classname, const char *model, float radius, float scale)
{
	edict_t *ed = ED_Alloc ();
	ed->v.classname = PR_SetEngineString (classname);
	ed->v.movetype = MOVETYPE_NONE;
	ed->v.model = PR_SetEngineString (model);
	ed->v.modelindex = SV_ModelIndex (model);
	for (int i = 0; i < 3; ++i)
	{
		ed->v.mins[i] = -radius;
		ed->v.maxs[i] = radius;
		ed->v.size[i] = radius * 2.0f;
	}
	eval_t *sc = GetEdictFieldValue (ed, qcvm->extfields.scale);
	if (sc && scale != 1.0f)
		sc->_float = scale;
	return ed;
}

static void SegmentSolid (wseg_t *s, float health)
{
	edict_t *ed = s->ent;
	ed->v.solid = SOLID_BBOX;
	ed->v.takedamage = DAMAGE_AIM;
	ed->v.health = ed->v.max_health = health;
	s->last_health = health;
	// T_Damage -> Killed calls th_die for MOVETYPE_NONE entities; a null function there is a QuakeC error
	dfunction_t *nop = ED_FindFunction ("SUB_Null");
	if (nop)
	{
		eval_t *die = GetEdictFieldValue (ed, ED_FindFieldOffset ("th_die"));
		eval_t *pain = GetEdictFieldValue (ed, ED_FindFieldOffset ("th_pain"));
		if (die)
			die->function = nop - qcvm->functions;
		if (pain)
			pain->function = nop - qcvm->functions;
	}
}

static void PoseSegments (void)
{
	vec3_t prev;
	VectorCopy (worm.head, prev);
	for (int i = 0; i < worm.nseg; ++i)
	{
		wseg_t	*s = &worm.seg[i];
		edict_t *ed = s->ent;
		float	 pos[3];
		if (i == 0)
			VectorCopy (worm.head, pos);
		else
			PathAt (i * worm.spacing, pos);
		vec3_t d;
		VectorSubtract (i == 0 ? worm.dir : prev, pos, d);
		if (i == 0)
			VectorCopy (worm.dir, d);
		else
		{
			VectorSubtract (prev, pos, d);
		}
		if (DotProduct (d, d) > 1e-4f)
		{
			ed->v.angles[1] = atan2f (d[1], d[0]) * 180.0f / M_PI;
			ed->v.angles[0] = atan2f (d[2], sqrtf (d[0] * d[0] + d[1] * d[1])) * 180.0f / M_PI;
		}
		VectorCopy (pos, prev);
		VectorCopy (pos, ed->v.origin);
		ed->v.velocity[0] = ed->v.velocity[1] = ed->v.velocity[2] = 0;
		int fx = 0;
		if (i == 0)
			fx |= EF_DIMLIGHT;
		if (qcvm->time < s->flash_until)
			fx |= EF_BRIGHTLIGHT;
		ed->v.effects = fx;
		SV_LinkEdict (ed, false);
	}
}

static float SpacingError (void)
{
	float worst = 0;
	for (int i = 0; i + 1 < worm.nseg; ++i)
		worst = q_max (worst, fabsf (Dist (worm.seg[i].ent->v.origin, worm.seg[i + 1].ent->v.origin) - worm.spacing));
	return worst;
}

static void Boom (const float p[3])
{
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_TAREXPLOSION);
	for (int i = 0; i < 3; ++i)
		MSG_WriteCoord (&sv.datagram, p[i], sv.protocolflags);
}

// Segments that took damage flash; dead ones go, and the chain closes up.
static void ProcessHealth (void)
{
	int out = 0;
	for (int i = 0; i < worm.nseg; ++i)
	{
		wseg_t *s = &worm.seg[i];
		if (s->ent->free)
		{
			if (i == 0)
			{
				worm.nseg = 0;
				WormKill ("head vanished");
				return;
			}
			continue;
		}
		if (s->ent->v.health <= 0)
		{
			Boom (s->ent->v.origin);
			SV_StartSound (s->ent, s->ent->v.origin, 2, SND_SEGDIE, 255, 0.8f);
			++worm.kills_segments;
			if (i == 0)
			{
				WormKill ("head killed");
				return;
			}
			ED_Free (s->ent);
			continue;
		}
		if (s->ent->v.health < s->last_health)
			s->flash_until = qcvm->time + 0.15;
		s->last_health = s->ent->v.health;
		worm.seg[out++] = *s;
	}
	worm.nseg = out;
	if (worm.nseg < 4)
		WormKill ("fewer than 4 segments");
}

static void HurtPlayer (edict_t *pl, edict_t *src)
{
	const float dmg = terrain_worm_damage.value;
	dfunction_t *td = ED_FindFunction ("T_Damage");
	if (td)
	{
		const int self = pr_global_struct->self, other = pr_global_struct->other;
		pr_global_struct->self = EDICT_TO_PROG (src);
		pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
		pr_global_struct->time = qcvm->time;
		G_INT (OFS_PARM0) = EDICT_TO_PROG (pl);
		G_INT (OFS_PARM1) = EDICT_TO_PROG (src);
		G_INT (OFS_PARM2) = EDICT_TO_PROG (src);
		G_FLOAT (OFS_PARM3) = dmg;
		PR_ExecuteProgram (td - qcvm->functions);
		pr_global_struct->self = self;
		pr_global_struct->other = other;
	}
	else
		pl->v.health -= dmg;
}

static void ContactDamage (void)
{
	if (!svs.maxclients || qcvm->time < worm.last_hit + 1.0)
		return;
	edict_t *pl = EDICT_NUM (1);
	if (pl->free || pl->v.health <= 0)
		return;
	const qboolean hunting = worm.hunt.active || worm.state == WS_AIR;
	for (int i = 0; i < worm.nseg; ++i)
	{
		if (i > 0 && !hunting)
			break;
		const float *c = worm.seg[i].ent->v.origin;
		float		 d2 = 0;
		for (int a = 0; a < 3; ++a)
		{
			const float lo = pl->v.origin[a] + pl->v.mins[a], hi = pl->v.origin[a] + pl->v.maxs[a];
			const float q = c[a] < lo ? lo - c[a] : c[a] > hi ? c[a] - hi : 0.0f;
			d2 += q * q;
		}
		if (d2 < worm.seg[i].radius * worm.seg[i].radius)
		{
			worm.last_hit = qcvm->time;
			++worm.hits;
			HurtPlayer (pl, worm.seg[0].ent);
			return;
		}
	}
}

/* ---- movement ---- */

static void Steer (const float want[3], float deg_per_sec, float dt)
{
	vec3_t w;
	VectorCopy (want, w);
	if (VectorNormalize (w) < 1e-4f)
		return;
	const float c = CLAMP (-1.0f, DotProduct (worm.dir, w), 1.0f);
	const float ang = acosf (c) * 180.0f / M_PI, maxa = deg_per_sec * dt;
	if (ang <= maxa)
	{
		VectorCopy (w, worm.dir);
		return;
	}
	const float t = maxa / ang;
	vec3_t		nd;
	if (c < -0.99f)
	{
		// head-on: pick a side
		const vec3_t side = {-worm.dir[1], worm.dir[0], 0.0f};
		VectorMA (worm.dir, t * 2.0f, side, nd);
	}
	else
		for (int i = 0; i < 3; ++i)
			nd[i] = worm.dir[i] * (1.0f - t) + w[i] * t;
	VectorNormalize (nd);
	VectorCopy (nd, worm.dir);
}

static void CruiseTarget (const float player[3], float tgt[3])
{
	const double t = qcvm->time;
	const float	 r = 300.0f + 200.0f * (0.5f + 0.5f * sinf (t * 0.23f));
	tgt[0] = player[0] + cosf (worm.theta) * r;
	tgt[1] = player[1] + sinf (worm.theta) * r;
	tgt[2] = player[2] - 40.0f + 120.0f * sinf (t * 0.31f);
}

static void HuntEnd (void)
{
	worm.hunt.active = 0;
	worm.hunt.warn_off = qcvm->time + 0.5;
	worm.next_hunt = qcvm->time + terrain_worm_hunt_min.value + WRand () * (terrain_worm_hunt_max.value - terrain_worm_hunt_min.value);
}

static void Dig (void)
{
	++worm.dig_attempts;
	if (worm.ops >= (int)terrain_worm_max_ops.value)
		return;
	const float before = Terrain_SampleWorld (terrain_active, worm.head);
	if (SV_Terrain_Edit (TERRAIN_OP_DIG, worm.head, terrain_worm_dig_radius.value) > 0)
	{
		if (!worm.ops)
		{
			VectorCopy (worm.head, worm.first_dig);
			worm.first_before = before;
		}
		++worm.ops;
		VectorCopy (worm.head, worm.last_dig);
		if (before < 0 && worm.num_digs < WORM_MAX_DIGS)
			VectorCopy (worm.head, worm.digs[worm.num_digs++]);
	}
}

static void EnterAir (qboolean by_field)
{
	if (by_field)
		++worm.air_field;
	else
		++worm.air_run_n;
	worm.air_hunt += worm.hunt.active;
	worm.state = WS_AIR;
	worm.air_run = 0;
	++worm.bursts;
	float v = worm.speed;
	if (worm.hunt.active)
	{
		v = q_max (v, terrain_worm_launch.value);
		if (Dist (worm.head, worm.hunt.E) < 320.0f)
		{
			// correct the launch so the arc passes the aim point above the player
			const float g = terrain_worm_gravity.value;
			const float d = Dist (worm.head, worm.hunt.T);
			const float t = CLAMP (0.45f, d / 620.0f, 1.4f);
			vec3_t		nv;
			for (int i = 0; i < 3; ++i)
				nv[i] = (worm.hunt.T[i] - worm.head[i]) / t;
			nv[2] += 0.5f * g * t;
			const float sp = sqrtf (DotProduct (nv, nv));
			if (sp < v && sp > 1.0f)
				VectorScale (nv, v / sp, nv);
			VectorCopy (nv, worm.vel);
			SV_StartSound (worm.seg[0].ent, worm.seg[0].ent->v.origin, 1, SND_BURST, 255, 0.5f);
			return;
		}
	}
	VectorScale (worm.dir, v, worm.vel);
	SV_StartSound (worm.seg[0].ent, worm.seg[0].ent->v.origin, 1, SND_BURST, 255, 0.5f);
}

static void StepWorm (float dt, const float player[3])
{
	const float open_field = 52.0f; // beyond what any tunnel stamp can read
	if (worm.state == WS_AIR && terrain_worm_freeze.value)
		return;
	if (worm.state == WS_AIR)
	{
		worm.vel[2] -= terrain_worm_gravity.value * dt;
		VectorMA (worm.head, dt, worm.vel, worm.head);
		const float sp = sqrtf (DotProduct (worm.vel, worm.vel));
		if (sp > 1.0f)
			VectorScale (worm.vel, 1.0f / sp, worm.dir);
		PushPath ();
		if (Terrain_SampleWorld (terrain_active, worm.head) < 0)
		{
			worm.state = WS_BURROW;
			worm.speed = CLAMP (terrain_worm_speed.value, sp, terrain_worm_hunt_speed.value);
			worm.last_dig[0] = worm.last_dig[1] = worm.last_dig[2] = -1e9f; // stamp at once: the tunnel mouth
			worm.air_run = 0;
			if (worm.hunt.active)
				HuntEnd ();
		}
		return;
	}
	float lo[3], hi[3], tgt[3];
	TerrainBounds (lo, hi, 96.0f);
	qboolean outside = false;
	for (int i = 0; i < 3; ++i)
		outside |= worm.head[i] < lo[i] || worm.head[i] > hi[i];
	if (outside)
	{
		for (int i = 0; i < 3; ++i)
			tgt[i] = (lo[i] + hi[i]) * 0.5f;
	}
	else if (worm.hunt.active)
	{
		if (worm.hunt.phase == 0 && Dist (worm.head, worm.hunt.S) < 150.0f)
			worm.hunt.phase = 1;
		if (worm.hunt.phase == 0)
			VectorCopy (worm.hunt.S, tgt);
		else
			VectorMA (worm.hunt.E, 200.0f, worm.hunt.v, tgt);
	}
	else
		CruiseTarget (player, tgt);
	if (!outside)
		for (int i = 0; i < 3; ++i)
			tgt[i] = CLAMP (lo[i], tgt[i], hi[i]);
	const qboolean hunt = worm.hunt.active;
	worm.speed = hunt ? terrain_worm_hunt_speed.value : terrain_worm_speed.value;
	vec3_t want;
	VectorSubtract (tgt, worm.head, want);
	Steer (want, hunt ? terrain_worm_hunt_turn.value : terrain_worm_turn.value, dt);
	VectorMA (worm.head, worm.speed * dt, worm.dir, worm.head);
	PushPath ();

	const float s = Terrain_SampleWorld (terrain_active, worm.head);
	const float dl = Dist (worm.head, worm.last_dig);
	const qboolean can_dig = worm.ops < (int)terrain_worm_max_ops.value;
	if (dl >= terrain_worm_dig_radius.value + 4.0f)
	{
		// outside our own newest stamp: air here is real open space or an older tunnel
		if (s > 8.0f && can_dig)
			worm.air_run += worm.speed * dt;
		else if (s <= 0.0f)
			worm.air_run = 0;
		if (s > open_field || worm.air_run >= 96.0f)
		{
			EnterAir (s > open_field);
			return;
		}
	}
	if (dl >= terrain_worm_dig_step.value && can_dig)
		Dig ();
	else if (!can_dig && dl >= terrain_worm_dig_step.value)
		VectorCopy (worm.head, worm.last_dig); // no more stamps; keep moving through whatever is there
}

/* ---- hunting ---- */

static qboolean PlanHunt (void)
{
	edict_t *pl = EDICT_NUM (1);
	float	 eye[3], P[3];
	VectorCopy (pl->v.origin, P);
	VectorAdd (pl->v.origin, pl->v.view_ofs, eye);
	const float yaw = pl->v.v_angle[1];
	float		best = -1e9f;
	qboolean	have = false;
	for (int c = 0; c < 40; ++c)
	{
		const float dyaw = (WRand () - 0.5f) * 150.0f, pitch = (WRand () - 0.6f) * 50.0f;
		vec3_t		ang = {-pitch, yaw + dyaw, 0}, fwd, r, u, p;
		AngleVectors (ang, fwd, r, u);
		if (Terrain_SampleWorld (terrain_active, eye) <= 1.0f)
			return false;
		float tr = 0;
		qboolean hit = false;
		while (tr < 900.0f)
		{
			VectorMA (eye, tr, fwd, p);
			const float d = Terrain_SampleWorld (terrain_active, p);
			if (d <= 1.0f)
			{
				hit = true;
				break;
			}
			tr += q_max (d * 0.8f, 3.0f);
		}
		if (!hit || tr < 128.0f)
			continue;
		float n[3];
		Gradient (p, n);
		float T[3] = {P[0], P[1], P[2] + 32.0f + terrain_worm_aim_height.value};
		const float g = terrain_worm_gravity.value, t = CLAMP (0.45f, Dist (p, T) / 620.0f, 1.4f);
		vec3_t		v;
		for (int i = 0; i < 3; ++i)
			v[i] = (T[i] - p[i]) / t;
		v[2] += 0.5f * g * t;
		VectorNormalize (v);
		const float out = DotProduct (v, n), score = cosf (dyaw * M_PI / 180.0f) + 2.0f * q_min (out, 0.7f) - fabsf (tr - 350.0f) / 500.0f - (out < 0.25f ? 3.0f : 0.0f);
		if (score > best)
		{
			best = score;
			have = true;
			VectorCopy (p, worm.hunt.E);
			VectorCopy (n, worm.hunt.n);
			VectorCopy (T, worm.hunt.T);
			VectorCopy (v, worm.hunt.v);
		}
	}
	if (!have)
		return false;
	const float L = terrain_worm_hunt_speed.value * 1.7f;
	VectorMA (worm.hunt.E, -L, worm.hunt.v, worm.hunt.S);
	return true;
}

static void StartHunt (void)
{
	if (!worm.active)
		return;
	FreeWarn ();
	worm.hunt.active = 0;
	if (!PlanHunt ())
	{
		worm.next_hunt = qcvm->time + 1.0;
		return;
	}
	worm.hunt.active = 1;
	worm.hunt.phase = 0;
	worm.hunt.warned = 0;
	worm.hunt.next_snd = 0;
	worm.hunt.deadline = qcvm->time + 16.0;
	++worm.hunts;
}

static void UpdateWarning (void)
{
	const double now = qcvm->time;
	if (worm.hunt.warn && !worm.hunt.active && now > worm.hunt.warn_off)
		FreeWarn ();
	if (!worm.hunt.active)
		return;
	if (now > worm.hunt.deadline)
	{
		HuntEnd ();
		return;
	}
	const float dist = Dist (worm.head, worm.hunt.E);
	const float eta = dist / q_max (terrain_worm_hunt_speed.value, 1.0f);
	if (!worm.hunt.warned && worm.state == WS_BURROW && eta <= terrain_worm_telegraph.value)
	{
		worm.hunt.warned = 1;
		worm.hunt.warn = SpawnEdict ("terrain_worm_warn", WARN_MODEL, 4.0f, 1.0f);
		worm.hunt.warn->v.solid = SOLID_NOT;
		VectorMA (worm.hunt.E, 24.0f, worm.hunt.n, worm.hunt.warn->v.origin);
		SV_LinkEdict (worm.hunt.warn, false);
	}
	if (worm.hunt.warn && !worm.hunt.warn->free)
	{
		const float k = CLAMP (0.0f, 1.0f - eta / terrain_worm_telegraph.value, 1.0f);
		const float hz = 2.0f + 8.0f * k;
		worm.hunt.warn->v.effects = sinf (now * hz * 2.0 * M_PI) > 0 ? EF_BRIGHTLIGHT : 0;
		if (now >= worm.hunt.next_snd && worm.state == WS_BURROW)
		{
			SV_StartSound (worm.hunt.warn, worm.hunt.warn->v.origin, 1, SND_WARN, 255, 0.5f);
			worm.hunt.next_snd = now + 0.55 - 0.43 * k;
		}
	}
	if (worm.state == WS_AIR)
		worm.hunt.warn_off = now + 0.4; // let the marker linger a moment
}

static qboolean worm_pending; // terrain_worm 1: spawn on the first server frame after the map load
static qboolean WormSpawn (const float near[3], float min_dist, float max_dist);

void SV_Terrain_WormFrame (void)
{
	if (worm_pending && terrain_active && sv.active)
	{
		// the map-load hook runs before the server is active, so the spawn waits for the first frame
		worm_pending = false;
		WormSpawn (EDICT_NUM (1)->v.origin, 600.0f, 1600.0f);
	}
	if (!worm.active)
		return;
	if (!terrain_active || !sv.active)
	{
		SV_Terrain_WormReset ();
		return;
	}
	ProcessHealth ();
	if (!worm.active)
		return;
	edict_t *pl = EDICT_NUM (1);
	const float *player = pl->v.origin;
	double		 dt = CLAMP (0.0, host_frametime, 0.1);
	if (!worm.hunt.active && terrain_worm_auto.value && qcvm->time >= worm.next_hunt)
		StartHunt ();
	worm.theta += 0.35f * (float)dt;
	const int steps = q_max (1, (int)ceil (dt / 0.02));
	for (int i = 0; i < steps; ++i)
		StepWorm ((float)(dt / steps), player);
	PoseSegments ();
	UpdateWarning ();
	ContactDamage ();
	worm.max_err_since = q_max (worm.max_err_since, SpacingError ());
}

/* ---- spawning ---- */

static qboolean WormSpawn (const float near[3], float min_dist, float max_dist)
{
	if (!terrain_active || !sv.active)
		return false;
	if (worm.active)
		WormKill ("replaced");
	float lo[3], hi[3], at[3];
	TerrainBounds (lo, hi, 192.0f);
	qboolean found = false;
	if (!worm.rng)
		worm.rng = 2463534242u;
	for (int i = 0; i < 3000 && !found; ++i)
	{
		for (int a = 0; a < 3; ++a)
			at[a] = lo[a] + (hi[a] - lo[a]) * WRand ();
		const float d = Dist (at, near);
		found = d >= min_dist && d <= max_dist && Terrain_SampleWorld (terrain_active, at) < -50.0f;
	}
	if (!found)
	{
		Con_Printf ("terrain_worm: found no rock to start in\n");
		return false;
	}
	const int n = CLAMP (5, (int)terrain_worm_segments.value, WORM_MAX_SEG);
	const int stats[4] = {0, 0, 0, 0};
	(void)stats;
	memset (&worm, 0, sizeof (worm) - 0);
	worm.rng = (uint32_t)(Sys_DoubleTime () * 1000.0) | 1u;
	VectorCopy (at, worm.head);
	const float yaw = WRand () * 2.0f * M_PI;
	worm.dir[0] = cosf (yaw);
	worm.dir[1] = sinf (yaw);
	worm.speed = terrain_worm_speed.value;
	worm.spacing = terrain_worm_spacing.value;
	// seed the path straight behind the head so the body starts in a line
	const int seed = 80;
	for (int k = 0; k <= seed; ++k)
	{
		VectorMA (worm.head, -(float)(seed - k) * WORM_PATH_GAP, worm.dir, worm.path[k]);
		worm.plen[k] = k * WORM_PATH_GAP;
	}
	worm.pn = seed + 1;
	worm.last_dig[0] = worm.last_dig[1] = worm.last_dig[2] = -1e9f;
	for (int i = 0; i < n; ++i)
	{
		wseg_t *s = &worm.seg[worm.nseg++];
		s->kind = i == 0 ? SEG_HEAD : i == n - 1 ? SEG_TAIL : SEG_BODY;
		s->radius = s->kind == SEG_HEAD ? 22.0f : s->kind == SEG_TAIL ? 10.0f : 16.0f;
		const float hp = s->kind == SEG_HEAD ? 400.0f : s->kind == SEG_TAIL ? 200.0f : 120.0f;
		if (s->kind == SEG_HEAD)
			s->ent = SpawnEdict ("terrain_worm_head", HEAD_MODEL, s->radius, s->radius / 27.0f);
		else
			s->ent = SpawnEdict ("terrain_worm_seg", BODY_MODEL, s->radius, s->radius / 7.5f);
		SegmentSolid (s, hp);
	}
	worm.active = 1;
	worm.next_hunt = qcvm->time + 3.0;
	PoseSegments ();
	Con_Printf ("terrain_worm: spawned %d segments at %.0f %.0f %.0f\n", n, at[0], at[1], at[2]);
	return true;
}

void SV_Terrain_WormMapLoad (const float start[3])
{
	// while the map loads, so every client has them
	SV_Precache_Model (HEAD_MODEL);
	SV_Precache_Model (BODY_MODEL);
	SV_Precache_Model (WARN_MODEL);
	SV_Precache_Sound (SND_WARN);
	SV_Precache_Sound (SND_BURST);
	SV_Precache_Sound (SND_SEGDIE);
	SV_Precache_Sound (SND_WORMDIE);
	(void)start;
	worm_pending = terrain_worm.value != 0;
}

/* ---- commands ---- */

static qboolean Ready (void)
{
	if (!terrain_active || !sv.active || !sv_player)
	{
		Con_Printf ("terrain_worm: needs a terrain and a local server\n");
		return false;
	}
	return true;
}

static void Spawn_f (void)
{
	if (!Ready ())
		return;
	PR_SwitchQCVM (&sv.qcvm);
	WormSpawn (EDICT_NUM (1)->v.origin, Cmd_Argc () > 1 ? atof (Cmd_Argv (1)) : 500.0f, 1400.0f);
	PR_SwitchQCVM (NULL);
}

static void Hunt_f (void)
{
	if (!Ready ())
		return;
	PR_SwitchQCVM (&sv.qcvm);
	if (worm.active)
		StartHunt ();
	Con_Printf ("terrain_worm_hunt: %s\n", worm.hunt.active ? "hunting" : "no plan found");
	PR_SwitchQCVM (NULL);
}

static void Hurt_f (void)
{
	if (!Ready () || !worm.active || Cmd_Argc () < 3)
	{
		Con_Printf ("usage: terrain_worm_hurt <segment> <damage> [qc] (0 is the head; qc goes through T_Damage)\n");
		return;
	}
	PR_SwitchQCVM (&sv.qcvm);
	const int i = atoi (Cmd_Argv (1));
	if (i >= 0 && i < worm.nseg && Cmd_Argc () > 3 && !strcmp (Cmd_Argv (3), "qc"))
	{
		// through the progs' own T_Damage, as a weapon does: exercises Killed and th_die
		edict_t *pl = EDICT_NUM (1);
		dfunction_t *td = ED_FindFunction ("T_Damage");
		if (td)
		{
			const int self = pr_global_struct->self, other = pr_global_struct->other;
			pr_global_struct->self = EDICT_TO_PROG (pl);
			pr_global_struct->time = qcvm->time;
			G_INT (OFS_PARM0) = EDICT_TO_PROG (worm.seg[i].ent);
			G_INT (OFS_PARM1) = EDICT_TO_PROG (pl);
			G_INT (OFS_PARM2) = EDICT_TO_PROG (pl);
			G_FLOAT (OFS_PARM3) = atof (Cmd_Argv (2));
			PR_ExecuteProgram (td - qcvm->functions);
			pr_global_struct->self = self;
			pr_global_struct->other = other;
		}
	}
	else if (i >= 0 && i < worm.nseg)
		worm.seg[i].ent->v.health -= atof (Cmd_Argv (2));
	PR_SwitchQCVM (NULL);
}

// Debug: turn the player to face a segment, so a scripted weapon test has something to shoot at.
static void Aim_f (void)
{
	if (!Ready () || !worm.active)
		return;
	PR_SwitchQCVM (&sv.qcvm);
	const int	 i = CLAMP (0, Cmd_Argc () > 1 ? atoi (Cmd_Argv (1)) : 0, worm.nseg - 1);
	edict_t	*pl = EDICT_NUM (1);
	vec3_t		 eye, d;
	VectorAdd (pl->v.origin, pl->v.view_ofs, eye);
	VectorSubtract (worm.seg[i].ent->v.origin, eye, d);
	pl->v.angles[0] = pl->v.v_angle[0] = -atan2f (d[2], sqrtf (d[0] * d[0] + d[1] * d[1])) * 180.0f / M_PI;
	pl->v.angles[1] = pl->v.v_angle[1] = atan2f (d[1], d[0]) * 180.0f / M_PI;
	pl->v.fixangle = 1;
	PR_SwitchQCVM (NULL);
}

static void Info_f (void)
{
	if (!Ready ())
		return;
	PR_SwitchQCVM (&sv.qcvm);
	const float err_now = worm.active ? SpacingError () : 0.0f;
	const float *h = worm.head;
	const vec3_t box_lo = {-16, -16, -24}, box_hi = {16, 16, 32};
	int			 fit = 0;
	for (int i = 0; i < worm.num_digs; ++i)
	{
		float o[3] = {worm.digs[i][0], worm.digs[i][1], worm.digs[i][2] - 4.0f}; // hull centre on the stamp
		fit += !Terrain_BoxOverlaps (terrain_active, o, box_lo, box_hi);
	}
	Con_Printf (
		"terrain_worm_info: active %d state %s head %.0f %.0f %.0f segs %d ops %d hunts %d bursts %d air_entries %d (open %d run %d, on a hunt %d) hits %d seg_kills %d hunting %d\n", worm.active,
		worm.state == WS_AIR ? "AIR" : "BURROW", h[0], h[1], h[2], worm.nseg, worm.ops, worm.hunts, worm.bursts, worm.bursts, worm.air_field, worm.air_run_n, worm.air_hunt, worm.hits, worm.kills_segments, worm.hunt.active);
	for (int i = 0; i < worm.nseg; ++i)
		Con_Printf ("%s%.0f", i ? " " : "terrain_worm_info: health ", worm.seg[i].ent->v.health);
	Con_Printf ("\n");
	Con_Printf ("terrain_worm_info: spacing_err now %.2f max_since_last %.2f\n", err_now, worm.max_err_since);
	worm.max_err_since = 0;
	if (worm.ops)
	{
		float p[3];
		VectorCopy (worm.first_dig, p);
		Con_Printf (
			"terrain_worm_info: tunnel first stamp %.0f %.0f %.0f was %.1f now %.1f; player hull fits at %d of %d stamped rock points\n", p[0], p[1], p[2], worm.first_before,
			Terrain_SampleWorld (terrain_active, p), fit, worm.num_digs);
	}
	PR_SwitchQCVM (NULL);
}

void SV_Terrain_WormInit (void)
{
	cvar_t *all[] = {&terrain_worm, &terrain_worm_auto, &terrain_worm_speed, &terrain_worm_hunt_speed, &terrain_worm_turn, &terrain_worm_hunt_turn, &terrain_worm_dig_radius,
					 &terrain_worm_dig_step, &terrain_worm_max_ops, &terrain_worm_launch, &terrain_worm_gravity, &terrain_worm_telegraph, &terrain_worm_hunt_min,
					 &terrain_worm_hunt_max, &terrain_worm_damage, &terrain_worm_aim_height, &terrain_worm_segments, &terrain_worm_freeze, &terrain_worm_spacing};
	for (size_t i = 0; i < sizeof (all) / sizeof (all[0]); ++i)
		Cvar_RegisterVariable (all[i]);
	Cmd_AddCommand ("terrain_worm_spawn", Spawn_f);
	Cmd_AddCommand ("terrain_worm_hunt", Hunt_f);
	Cmd_AddCommand ("terrain_worm_hurt", Hurt_f);
	Cmd_AddCommand ("terrain_worm_info", Info_f);
	Cmd_AddCommand ("terrain_worm_aim", Aim_f);
}
