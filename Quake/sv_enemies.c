/*
 * Two enemies for the cave, made in the engine from the running progs' own monsters (no QuakeC):
 *
 * Saucer (Agarthan Hyperborean flying saucer): the progs' scrag (monster_wizard) in a saucer's body,
 * flying and fighting like Arcane Dimensions' sentinel drone (from Quoth): it holds its height to
 * fire laser bolts at the sentinel's speeds (575 + 75 a skill level), and moves very fast when its
 * enemy is out of sight; here it is also bigger, tougher and quicker than a scrag, its bolts hit
 * harder, and out of sight it follows the cave's air routes (the crawlers' flow field) so it keeps
 * up with whoever it chases. With AD's data present it uses the sentinel's own sounds; otherwise
 * the scrag's and the enforcer's.
 *
 * Yeti: the progs' shambler with a fluffy white and ice-blue coat, fighting like Dawn of the
 * Machine's blood shambler (MG3's monster_super_shambler): 2000 health, and each smash throws a
 * fan of 11 snowballs 5 degrees apart, each claw a 3 x 3 block of 9 (9 damage each), on top of the
 * shambler's own blows. It never casts lightning: its ranged attack is the smash (its th_missile is
 * pointed at the progs' sham_smash1), which from afar hurls one big snowball instead. Snowballs that land leave snow on the ground as terrain fill
 * ops, so where a yeti fights, drifts build up. The coat is made when the model loads, from the player's own
 * shambler skin (gl_model.c, Mod_GenerateYetiModel): no id data is shipped.
 *
 * GPL-2.0-or-later, as the rest of the engine.
 */
#include "quakedef.h"
#include "terrain.h"

#define SAUCER_MODEL   "progs/workshop/saucer.mdl"
#define SAUCER_GIB	   "progs/workshop/saucergib.mdl"
#define SNOWBALL_MODEL "progs/workshop/snowball.mdl"
#define LASER_MODEL	   "progs/laser.mdl" // the enforcer's bolt, as AD's sentinel uses
#define YETI_MODEL	   "progs/workshop/yeti.mdl"

#define CHAN_AUTO	0
#define CHAN_WEAPON 1
#define ATTN_NORM	1

extern cvar_t sv_gravity;

static cvar_t terrain_saucers = {"terrain_saucers", "2", CVAR_NONE}; // saucers added to a drg* map
static cvar_t terrain_yetis = {"terrain_yetis", "1", CVAR_NONE};	 // yetis added to a drg* map
static cvar_t terrain_saucer_health = {"terrain_saucer_health", "150", CVAR_NONE};
static cvar_t terrain_saucer_speed = {"terrain_saucer_speed", "1.7", CVAR_NONE}; // times a scrag's, in sight
static cvar_t terrain_saucer_hunt = {"terrain_saucer_hunt", "2.6", CVAR_NONE};	 // times a scrag's, out of sight
static cvar_t terrain_saucer_laser = {"terrain_saucer_laser", "15", CVAR_NONE};	 // damage a bolt does
static cvar_t terrain_yeti_health = {"terrain_yeti_health", "2000", CVAR_NONE};
static cvar_t terrain_yeti_snow = {"terrain_yeti_snow", "10", CVAR_NONE}; // radius of the snow a landed snowball leaves; 0 none

enum
{
	KIND_SAUCER = 1,
	KIND_YETI
};

#define MAX_ENEMIES 32
#define MAX_SHOTS	128
typedef struct
{
	edict_t *ent;
	int		 kind, last_frame, dead;
	double	 fire_until;
} enemy_t;

typedef struct
{
	edict_t *ent, *owner;
	int		 kind; // KIND_SAUCER: a laser bolt; KIND_YETI: a snowball
	double	 born;
	float	 damage, snow; // a snowball's hurt, and the radius of the snow it leaves
} shot_t;

static enemy_t	enemies[MAX_ENEMIES];
static shot_t	shots[MAX_SHOTS];
static int		num_enemies, num_shots;
static int		fired_bolts, fired_snowballs, hits_bolts, hits_snowballs, snow_ops; // for terrain_ents
static qboolean have_saucer, have_snowball, have_gib, have_sentinel;

static enemy_t *EnemyFind (const edict_t *ent)
{
	for (int i = 0; i < num_enemies; ++i)
		if (enemies[i].ent == ent)
			return &enemies[i];
	return NULL;
}

void SV_Enemies_Clear (void)
{
	num_enemies = num_shots = 0;
	fired_bolts = fired_snowballs = hits_bolts = hits_snowballs = snow_ops = 0;
}

void SV_Enemies_Report (void)
{
	Con_Printf (
		"terrain_enemies: %d lasers fired (%d hit), %d snowballs thrown (%d hit), %d snow drifts\n", fired_bolts, hits_bolts, fired_snowballs, hits_snowballs,
		snow_ops);
}

// While a drg* map loads: the models and sounds the two can use.
void SV_Enemies_Precache (void)
{
	SV_Enemies_Clear ();
	have_saucer = COM_FileExists (SAUCER_MODEL, NULL);
	have_snowball = COM_FileExists (SNOWBALL_MODEL, NULL);
	have_gib = COM_FileExists (SAUCER_GIB, NULL);
	have_sentinel = COM_FileExists ("sound/sentinel/laser.wav", NULL); // Arcane Dimensions' data
	if (have_saucer)
		SV_Precache_Model (SAUCER_MODEL);
	if (have_snowball)
		SV_Precache_Model (SNOWBALL_MODEL);
	if (have_gib)
		SV_Precache_Model (SAUCER_GIB);
	if (COM_FileExists ("progs/shambler.mdl", NULL))
		SV_Precache_Model (YETI_MODEL);
	SV_Precache_Model (LASER_MODEL);
	SV_Precache_Sound ("enforcer/enfire.wav");
	SV_Precache_Sound ("enforcer/enfstop.wav");
	SV_Precache_Sound ("weapons/r_exp3.wav");
	SV_Precache_Sound ("zombie/z_shot1.wav");
	SV_Precache_Sound ("zombie/z_miss.wav");
	if (have_sentinel)
	{
		static const char *const sentinel[] = {"sentinel/laser.wav", "sentinel/widle1.wav", "sentinel/widle2.wav", "sentinel/wsight.wav", "sentinel/wpain.wav"};
		for (size_t i = 0; i < sizeof (sentinel) / sizeof (sentinel[0]); ++i)
			if (COM_FileExists (va ("sound/%s", sentinel[i]), NULL))
				SV_Precache_Sound (sentinel[i]);
	}
}

static void SetModel (edict_t *ent, const char *model)
{
	ent->v.model = PR_SetEngineString (model);
	ent->v.modelindex = SV_ModelIndex (model);
}

static void SetScale (edict_t *ent, float scale)
{
	eval_t *val = GetEdictFieldValue (ent, qcvm->extfields.scale);
	if (val)
		val->_float = scale;
}

/*
Takes over a monster the progs just spawned: a scrag becomes a saucer, a shambler a yeti. False when
the monster is not of that kind or its look is missing (it then stays what it was).
*/
int SV_Enemies_Adopt (edict_t *ent, int yeti)
{
	if (num_enemies == MAX_ENEMIES || ent->free)
		return 0;
	if (yeti)
	{
		if (!COM_FileExists ("progs/shambler.mdl", NULL))
			return 0;
		SetModel (ent, YETI_MODEL); // the shambler's frames, so its own animations play on it
		ent->v.health = ent->v.max_health = terrain_yeti_health.value;
		// no lightning: its ranged attack is the smash, which hurls a big snowball from afar
		const int	 missile = ED_FindFieldOffset ("th_missile");
		dfunction_t *smash = ED_FindFunction ("sham_smash1");
		eval_t		*field = missile >= 0 ? GetEdictFieldValue (ent, missile) : NULL;
		if (field && smash)
			field->function = smash - qcvm->functions;
	}
	else
	{
		if (!have_saucer)
			return 0;
		SetModel (ent, SAUCER_MODEL);
		const vec3_t mins = {-30, -30, -12}, maxs = {30, 30, 14};
		VectorCopy (mins, ent->v.mins);
		VectorCopy (maxs, ent->v.maxs);
		VectorSubtract (maxs, mins, ent->v.size);
		ent->v.health = ent->v.max_health = terrain_saucer_health.value;
		SV_LinkEdict (ent, false);
	}
	enemy_t *e = &enemies[num_enemies++];
	memset (e, 0, sizeof (*e));
	e->ent = ent;
	e->kind = yeti ? KIND_YETI : KIND_SAUCER;
	e->last_frame = -1;
	return 1;
}

int SV_Enemies_IsSaucer (const edict_t *ent)
{
	const enemy_t *e = num_enemies ? EnemyFind (ent) : NULL;
	return e && e->kind == KIND_SAUCER && !e->dead;
}

/*
The saucer's sounds: the scrag's swapped for the sentinel's when AD is there, its spit's for a laser.
Called by SV_StartSound with the sample it is about to play.
*/
const char *SV_Enemies_Sound (const edict_t *ent, const char *sample)
{
	const enemy_t *e = num_enemies && ent ? EnemyFind (ent) : NULL;
	if (!e || e->kind != KIND_SAUCER || strncmp (sample, "wizard/", 7))
		return sample;
	const char *name = sample + 7;
	if (!strcmp (name, "wattack.wav"))
		return have_sentinel && (COM_Rand () % 5) == 0 ? "sentinel/laser.wav" : "enforcer/enfire.wav";
	if (!strcmp (name, "wdeath.wav"))
		return "weapons/r_exp3.wav";
	if (have_sentinel && (!strcmp (name, "widle1.wav") || !strcmp (name, "widle2.wav") || !strcmp (name, "wsight.wav") || !strcmp (name, "wpain.wav")))
	{
		static char mapped[64];
		q_snprintf (mapped, sizeof (mapped), "sentinel/%s", name);
		return mapped;
	}
	return sample;
}

/*
The saucer's flight, from SV_MoveToGoal. In sight of its target it chases as a scrag does, only
quicker; out of sight it flies the cave's air route toward the players, quicker still, the way the
sentinel races when it cannot see you. True when it moved here.
*/
int SV_Enemies_MoveToGoal (edict_t *ent, edict_t *goal, float dist)
{
	if (!SV_Enemies_IsSaucer (ent) || goal == qcvm->edicts || goal->free)
		return 0;
	vec3_t eye, target;
	VectorCopy (ent->v.origin, eye);
	VectorAdd (goal->v.origin, goal->v.view_ofs, target);
	const trace_t  sight = SV_Move (eye, vec3_origin, vec3_origin, target, MOVE_NOMONSTERS, ent);
	const qboolean seen = sight.fraction == 1.0f || sight.ent == goal;
	if (!seen)
	{
		float route[3];
		if (SV_Terrain_AirRoute (ent->v.origin, route))
		{
			vec3_t move, end;
			VectorNormalize (route);
			VectorScale (route, dist * terrain_saucer_hunt.value, move);
			VectorAdd (ent->v.origin, move, end);
			const trace_t tr = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);
			if (!tr.startsolid && tr.fraction > 0.0f)
			{
				VectorCopy (tr.endpos, ent->v.origin);
				ent->v.ideal_yaw = anglemod (atan2f (route[1], route[0]) * 180.0f / M_PI);
				SV_LinkEdict (ent, true);
				return 1;
			}
		}
	}
	// the scrag's own chase, a step bigger
	const float far = dist * terrain_saucer_speed.value;
	if ((COM_Rand () & 3) == 1 || !SV_StepDirection (ent, ent->v.ideal_yaw, far))
		SV_NewChaseDir (ent, goal, far);
	return 1;
}

// T_Damage (targ, inflictor, attacker, damage) from the running progs.
static void Damage (edict_t *targ, edict_t *inflictor, edict_t *attacker, float damage)
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

static shot_t *NewShot (edict_t *owner, int kind)
{
	if (num_shots == MAX_SHOTS)
		return NULL;
	edict_t *ent = ED_Alloc ();
	ent->v.movetype = MOVETYPE_NONE; // moved in SV_Enemies_Frame
	ent->v.solid = SOLID_NOT;
	ent->v.owner = EDICT_TO_PROG (owner);
	shot_t *s = &shots[num_shots++];
	s->ent = ent;
	s->owner = owner;
	s->kind = kind;
	s->born = qcvm->time;
	return s;
}

// A scrag's spit, fired by a saucer: becomes a laser bolt at the sentinel's speed (it is removed here).
static void Saucer_Bolt (edict_t *spit, edict_t *saucer)
{
	shot_t *s = NewShot (saucer, KIND_SAUCER);
	if (!s)
		return;
	edict_t *bolt = s->ent;
	vec3_t	 dir, aim;
	bolt->v.classname = PR_SetEngineString ("saucer_laser");
	++fired_bolts;
	SetModel (bolt, LASER_MODEL);
	bolt->v.effects = EF_DIMLIGHT;
	// out of the turret under its belly, aimed at the target's body (leading it a little), not
	// along the scrag's spit, which leaves from beside the scrag with a spread of its own
	VectorCopy (saucer->v.origin, bolt->v.origin);
	bolt->v.origin[2] -= 8.0f;
	const float	   speed = (575.0f + 75.0f * CLAMP (0.0f, skill.value, 3.0f)) * 1.25f;
	const edict_t *enemy = PROG_TO_EDICT (saucer->v.enemy);
	if (enemy != qcvm->edicts && !enemy->free)
	{
		VectorCopy (enemy->v.origin, aim);
		aim[2] += (enemy->v.mins[2] + enemy->v.maxs[2]) * 0.5f;
		VectorSubtract (aim, bolt->v.origin, dir);
		const float flight = VectorLength (dir) / speed;
		VectorMA (aim, flight * 0.5f, enemy->v.velocity, aim);
		VectorSubtract (aim, bolt->v.origin, dir);
	}
	else
		VectorCopy (spit->v.velocity, dir);
	if (VectorNormalize (dir) < 0.001f)
		return;
	VectorScale (dir, speed, bolt->v.velocity);
	VectorAngles (dir, NULL, bolt->v.angles);
	SV_LinkEdict (bolt, false);
}

// A yeti's throw: a snowball along dir at speed, rising up (MG3's numbers).
static void Yeti_Snowball (edict_t *yeti, const vec3_t org, const vec3_t dir, float speed, float up, float size)
{
	shot_t *s = NewShot (yeti, KIND_YETI);
	if (!s)
		return;
	s->damage = 9.0f * size * size;
	s->snow = terrain_yeti_snow.value * size;
	SetScale (s->ent, size);
	edict_t *ball = s->ent;
	ball->v.classname = PR_SetEngineString ("yeti_snowball");
	++fired_snowballs;
	SetModel (ball, have_snowball ? SNOWBALL_MODEL : "progs/grenade.mdl");
	VectorCopy (org, ball->v.origin);
	VectorScale (dir, speed, ball->v.velocity);
	ball->v.velocity[2] = up;
	ball->v.avelocity[0] = (float)(COM_Rand () % 600) - 300.0f;
	ball->v.avelocity[1] = (float)(COM_Rand () % 600) - 300.0f;
	SV_LinkEdict (ball, false);
}

static float CRandom (void)
{
	return (float)(COM_Rand () % 2001) / 1000.0f - 1.0f;
}

// The smash's fan (11 snowballs 5 degrees apart) or a claw's block (3 x 3, about 12 degrees apart).
static void Yeti_Throw (edict_t *yeti, qboolean fan)
{
	edict_t *enemy = PROG_TO_EDICT (yeti->v.enemy);
	if (enemy == qcvm->edicts || enemy->free)
		return;
	vec3_t centre, to, base;
	for (int a = 0; a < 3; ++a)
		centre[a] = yeti->v.origin[a] + (yeti->v.mins[a] + yeti->v.maxs[a]) * 0.5f;
	VectorSubtract (enemy->v.origin, centre, to);
	VectorAngles (to, NULL, base);
	const float far = VectorLength (to);
	if (fan && far > 160.0f)
	{
		// out of reach: one big snowball, lobbed to come down on them (flight time from the distance)
		vec3_t fwd, right, up, org, vel;
		AngleVectors (base, fwd, right, up);
		VectorMA (centre, 24.0f, fwd, org);
		org[2] += 30.0f;
		const float speed = 650.0f, flight = far / speed;
		VectorSubtract (enemy->v.origin, org, vel);
		vel[2] = 0;
		VectorScale (vel, 1.0f / q_max (flight, 0.05f), vel);
		vel[2] = (enemy->v.origin[2] - org[2]) / q_max (flight, 0.05f) + 0.5f * sv_gravity.value * flight;
		const float len = VectorNormalize (vel);
		Yeti_Snowball (yeti, org, vel, len, vel[2] * len, 2.5f);
		SV_StartSound (yeti, NULL, CHAN_WEAPON, "zombie/z_shot1.wav", 255, ATTN_NORM);
		return;
	}
	for (int x = fan ? -5 : -1; x <= (fan ? 5 : 1); ++x)
		for (int y = fan ? 0 : -1; y <= (fan ? 0 : 1); ++y)
		{
			vec3_t ang, fwd, right, up, org;
			VectorCopy (base, ang);
			ang[YAW] += fan ? (float)x * 5.0f : (float)x * (12.0f + CRandom () * 4.0f);
			ang[PITCH] += fan ? 0.0f : (float)y * (12.0f + CRandom () * 4.0f);
			AngleVectors (ang, fwd, right, up);
			VectorMA (centre, 20.0f, fwd, org);
			if (fan)
				Yeti_Snowball (yeti, org, fwd, CRandom () * 100.0f + 400.0f, 125.0f + CRandom () * 50.0f + fwd[2] * 400.0f, 1.0f);
			else
				Yeti_Snowball (yeti, org, fwd, CRandom () * 100.0f + 500.0f, 100.0f + CRandom () * 50.0f + fwd[2] * 500.0f, 1.0f);
		}
	SV_StartSound (yeti, NULL, CHAN_WEAPON, "zombie/z_shot1.wav", 255, ATTN_NORM);
}

// A saucer brought down: a blast and its hull in pieces; the scrag's body goes on unseen.
static void Saucer_Wreck (edict_t *saucer)
{
	MSG_WriteByte (&sv.datagram, svc_temp_entity);
	MSG_WriteByte (&sv.datagram, TE_EXPLOSION);
	for (int a = 0; a < 3; ++a)
		MSG_WriteCoord (&sv.datagram, saucer->v.origin[a], sv.protocolflags);
	dfunction_t *remove = ED_FindFunction ("SUB_Remove");
	for (int i = 0; have_gib && i < 5; ++i)
	{
		edict_t *gib = ED_Alloc ();
		gib->v.classname = PR_SetEngineString ("saucer_gib");
		gib->v.movetype = MOVETYPE_BOUNCE;
		gib->v.solid = SOLID_NOT;
		SetModel (gib, SAUCER_GIB);
		VectorCopy (saucer->v.origin, gib->v.origin);
		gib->v.velocity[0] = CRandom () * 300.0f;
		gib->v.velocity[1] = CRandom () * 300.0f;
		gib->v.velocity[2] = 200.0f + CRandom () * 150.0f;
		gib->v.avelocity[0] = CRandom () * 600.0f;
		gib->v.avelocity[1] = CRandom () * 600.0f;
		if (remove)
		{
			gib->v.think = remove - qcvm->functions;
			gib->v.nextthink = qcvm->time + 3.0 + CRandom ();
		}
		SV_LinkEdict (gib, false);
	}
	saucer->v.model = 0;
	saucer->v.modelindex = 0;
	SV_LinkEdict (saucer, false);
}

static void Shots_Frame (void)
{
	const float dt = (float)host_frametime;
	for (int i = 0; i < num_shots;)
	{
		shot_t	*s = &shots[i];
		edict_t *ent = s->ent;
		qboolean done = ent->free || qcvm->time > s->born + 5.0;
		if (!done)
		{
			vec3_t end;
			if (s->kind == KIND_YETI)
				ent->v.velocity[2] -= sv_gravity.value * dt;
			VectorMA (ent->v.origin, dt, ent->v.velocity, end);
			trace_t tr = SV_Move (ent->v.origin, vec3_origin, vec3_origin, end, MOVE_MISSILE, s->owner);
			if (tr.fraction < 1.0f || tr.startsolid)
			{
				vec3_t dir;
				VectorCopy (ent->v.velocity, dir);
				VectorNormalize (dir);
				const qboolean body = tr.ent && tr.ent != qcvm->edicts && tr.ent->v.takedamage && tr.ent != s->owner;
				if (body)
					++*(s->kind == KIND_YETI ? &hits_snowballs : &hits_bolts);
				if (body)
					Damage (tr.ent, ent, s->owner->free ? qcvm->edicts : s->owner, s->kind == KIND_YETI ? s->damage : terrain_saucer_laser.value);
				if (s->kind == KIND_YETI)
				{
					// a snowball that lands on the cave or the map leaves its snow there: a small
					// fill op (logged and networked like any edit), so drifts build up where it throws
					if (!body && !tr.startsolid && s->snow > 0)
					{
						vec3_t at;
						VectorMA (tr.endpos, -s->snow * 0.35f, tr.plane.normal, at);
						snow_ops += SV_Terrain_Edit (TERRAIN_OP_FILL, at, s->snow) > 0;
					}
					SV_StartParticle (tr.endpos, dir, 12, 24); // a puff of snow (the palette's light greys)
					SV_StartSound (ent, NULL, CHAN_AUTO, "zombie/z_miss.wav", 200, ATTN_NORM);
				}
				else
				{
					SV_StartParticle (tr.endpos, dir, 0xd4, 12); // the bolt's sparks
					SV_StartSound (ent, NULL, CHAN_AUTO, "enforcer/enfstop.wav", 255, ATTN_NORM);
				}
				done = true;
			}
			else
			{
				VectorCopy (end, ent->v.origin);
				for (int a = 0; a < 3; ++a)
					ent->v.angles[a] = anglemod (ent->v.angles[a] + ent->v.avelocity[a] * dt);
				SV_LinkEdict (ent, false);
			}
		}
		if (done)
		{
			if (!ent->free)
				ED_Free (ent);
			*s = shots[--num_shots];
		}
		else
			++i;
	}
}

// Once a server frame, after the monsters have thought.
void SV_Enemies_Frame (void)
{
	if (!num_enemies && !num_shots)
		return;
	// a saucer's spit becomes a bolt the frame it is fired
	for (int e = svs.maxclients + 1; e < qcvm->num_edicts; ++e)
	{
		edict_t *spit = EDICT_NUM (e);
		// (the 1.06 progs call it "wizspike"; the 2021 release's is a plain spike in the scrag's model)
		if (spit->free || (strcmp (PR_GetString (spit->v.classname), "wizspike") && strcmp (PR_GetString (spit->v.model), "progs/w_spike.mdl")))
			continue;
		edict_t *owner = PROG_TO_EDICT (spit->v.owner);
		if (!SV_Enemies_IsSaucer (owner))
			continue;
		Saucer_Bolt (spit, owner);
		ED_Free (spit);
	}
	for (int i = 0; i < num_enemies;)
	{
		enemy_t *e = &enemies[i];
		edict_t *ent = e->ent;
		if (ent->free)
		{
			*e = enemies[--num_enemies];
			continue;
		}
		const int frame = (int)ent->v.frame;
		if (e->kind == KIND_SAUCER)
		{
			if (ent->v.health <= 0 && !e->dead)
			{
				e->dead = 1;
				Saucer_Wreck (ent);
			}
			else if (!e->dead)
			{
				// the scrag's animations are not the saucer's: it shows its idle or its firing tilt
				if (frame != e->last_frame && frame >= 28 && frame <= 33) // the scrag's attack frames
					e->fire_until = qcvm->time + 0.3;
				e->last_frame = frame;
				ent->v.frame = qcvm->time < e->fire_until ? 1 : 0;
				e->last_frame = frame;
				ent->v.angles[ROLL] = 0;
				ent->v.angles[PITCH] = 0;
				SetScale (ent, 1.0f);
			}
		}
		else if (e->kind == KIND_YETI && frame != e->last_frame && ent->v.health > 0)
		{
			// the shambler's blows: smash10 (frame 44), swingr7 (53) and swingl7 (62)
			if (frame == 44)
				Yeti_Throw (ent, true);
			else if (frame == 53 || frame == 62)
				Yeti_Throw (ent, false);
			e->last_frame = frame;
		}
		++i;
	}
	Shots_Frame ();
}

void SV_Enemies_Init (void)
{
	Cvar_RegisterVariable (&terrain_saucers);
	Cvar_RegisterVariable (&terrain_yetis);
	Cvar_RegisterVariable (&terrain_saucer_health);
	Cvar_RegisterVariable (&terrain_saucer_speed);
	Cvar_RegisterVariable (&terrain_saucer_hunt);
	Cvar_RegisterVariable (&terrain_saucer_laser);
	Cvar_RegisterVariable (&terrain_yeti_health);
	Cvar_RegisterVariable (&terrain_yeti_snow);
}

int SV_Enemies_WantSaucers (void)
{
	return (int)terrain_saucers.value;
}

int SV_Enemies_WantYetis (void)
{
	return (int)terrain_yetis.value;
}
