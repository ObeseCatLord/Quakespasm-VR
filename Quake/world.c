/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// world.c -- world query functions

#include "quakedef.h"

/*

entities never clip against themselves, or their owner

line of sight checks trace->crosscontent, but bullets don't

*/


typedef struct
{
	vec3_t		boxmins, boxmaxs;// enclose the test object along entire move
	float		*mins, *maxs;	// size of the moving object
	vec3_t		mins2, maxs2;	// size when clipping against mosnters
	float		*start, *end;
	trace_t		trace;
	int			type;
	edict_t		*passedict;
} moveclip_t;

static qboolean SV_IsActiveClientEdict (edict_t *ent)
{
	int entnum;

	if (!ent || ent->free)
		return false;

	entnum = NUM_FOR_EDICT(ent);
	if (entnum < 1 || entnum > svs.maxclients)
		return false;

	if (!svs.clients[entnum - 1].active || !svs.clients[entnum - 1].spawned)
		return false;

	return ((int)ent->v.flags & FL_CLIENT) != 0;
}

static qboolean SV_ShouldSkipRecentTeleportTrigger (edict_t *touch, edict_t *ent)
{
	const char	*classname;

	if (!SV_IsActiveClientEdict(ent))
		return false;
	if (ent->v.teleport_time <= qcvm->time)
		return false;
	if (!touch->v.classname)
		return false;

	classname = PR_GetString(touch->v.classname);
	if (!classname || !classname[0])
		return false;

	return !q_strcasecmp(classname, "trigger_teleport")
		|| q_strcasestr(classname, "teleport");
}

static qboolean SV_IsPointMove (moveclip_t *clip)
{
	return clip->mins[0] == clip->maxs[0]
		&& clip->mins[1] == clip->maxs[1]
		&& clip->mins[2] == clip->maxs[2];
}

static qboolean SV_ShouldSkipCoopPlayerClip (moveclip_t *clip, edict_t *touch)
{
	if (!sv_coop_noplayerclip.value || !coop.value)
		return false;
	if (!clip->passedict)
		return false;
	if (clip->type == MOVE_MISSILE)
		return false;
	if (SV_IsPointMove(clip))
		return false;	// point traces should still hit players

	return SV_IsActiveClientEdict(clip->passedict)
		&& SV_IsActiveClientEdict(touch);
}

static qboolean SV_EdictStringFieldSet (edict_t *ent, const char *fieldname)
{
	eval_t	*val;

	val = GetEdictFieldValueByName(ent, fieldname);
	return val && val->string && PR_GetString(val->string)[0];
}

#define SV_COOP_TARGET_FIELD_COUNT 5

static const char *sv_coop_target_fields[SV_COOP_TARGET_FIELD_COUNT] =
{
	"target",
	"killtarget",
	"target2",
	"target3",
	"target4"
};

typedef struct
{
	string_t	values[SV_COOP_TARGET_FIELD_COUNT];
	qboolean	has_any;
} sv_coop_target_state_t;

static string_t SV_EdictStringFieldValue (edict_t *ent, const char *fieldname)
{
	eval_t	*val;

	val = GetEdictFieldValueByName(ent, fieldname);
	if (!val || !val->string || !PR_GetString(val->string)[0])
		return 0;
	return val->string;
}

static void SV_CaptureCoopTargetState (edict_t *ent, sv_coop_target_state_t *state)
{
	int	i;

	memset(state, 0, sizeof(*state));
	for (i = 0; i < SV_COOP_TARGET_FIELD_COUNT; i++)
	{
		state->values[i] = SV_EdictStringFieldValue(ent, sv_coop_target_fields[i]);
		if (state->values[i])
			state->has_any = true;
	}
}

static qboolean SV_CoopTargetStateUnchanged (edict_t *ent, const sv_coop_target_state_t *state)
{
	int	i;

	for (i = 0; i < SV_COOP_TARGET_FIELD_COUNT; i++)
	{
		if (SV_EdictStringFieldValue(ent, sv_coop_target_fields[i]) != state->values[i])
			return false;
	}
	return true;
}

static qboolean SV_ClassnameMatchesList (const char *classname, const char *list)
{
	const char	*p;
	size_t		len;

	if (!classname || !classname[0] || !list || !list[0])
		return false;

	p = list;
	while (*p)
	{
		while (*p && ((unsigned char)*p <= ' ' || *p == ',' || *p == ';'))
			p++;
		if (!*p)
			break;

		len = 0;
		while (p[len] && (unsigned char)p[len] > ' ' && p[len] != ',' && p[len] != ';')
			len++;

		if (strlen(classname) == len && !q_strncasecmp(classname, p, len))
			return true;

		p += len;
	}

	return false;
}

static qboolean SV_CoopWeaponHasTargets (edict_t *weapon)
{
	return SV_EdictStringFieldSet(weapon, "target")
		|| SV_EdictStringFieldSet(weapon, "killtarget")
		|| SV_EdictStringFieldSet(weapon, "target2")
		|| SV_EdictStringFieldSet(weapon, "target3")
		|| SV_EdictStringFieldSet(weapon, "target4");
}

static qboolean SV_IsDirectWeaponTouch (func_t touchfunc)
{
	static dprograms_t	*cached_progs;
	static unsigned short	cached_crc;
	static func_t		weapon_touch;
	dfunction_t			*func;

	if (cached_progs != qcvm->progs || cached_crc != qcvm->crc)
	{
		cached_progs = qcvm->progs;
		cached_crc = qcvm->crc;
		func = ED_FindFunction("weapon_touch");
		weapon_touch = func ? (func_t)(func - qcvm->functions) : 0;
	}

	return weapon_touch && touchfunc == weapon_touch;
}

static qboolean SV_IsCoopWeaponTargetFixCandidate (edict_t *weapon, edict_t *player)
{
	const char	*classname;
	int		fixlevel;

	fixlevel = (int)sv_coop_weapon_targetfix.value;
	if (fixlevel <= 0 || !coop.value)
		return false;
	if (!SV_IsActiveClientEdict(player))
		return false;
	if (!weapon || weapon->free || weapon->v.solid != SOLID_TRIGGER)
		return false;

	classname = PR_GetString(weapon->v.classname);
	if (q_strncasecmp(classname, "weapon_", 7))
		return false;

	// Level 1 preserves the original conservative behavior: only the common
	// QuakeC weapon_touch path is patched. Level 2 also covers custom weapon
	// touch handlers, but still verifies after the touch that the mod did not
	// consume or rewrite the targets itself.
	if (fixlevel < 2 && !SV_IsDirectWeaponTouch(weapon->v.touch))
		return false;

	return SV_CoopWeaponHasTargets(weapon);
}

static qboolean SV_IsCoopPickupTargetFixCandidate (edict_t *pickup, edict_t *player)
{
	const char	*classname;

	if (!sv_coop_pickup_targetfix.value || !coop.value)
		return false;
	if (!SV_IsActiveClientEdict(player))
		return false;
	if (!pickup || pickup->free || pickup->v.solid != SOLID_TRIGGER)
		return false;
	if (!pickup->v.classname)
		return false;

	classname = PR_GetString(pickup->v.classname);
	if (!q_strncasecmp(classname, "weapon_", 7))
		return false; // handled by sv_coop_weapon_targetfix
	if (q_strncasecmp(classname, "item_", 5) && q_strncasecmp(classname, "ammo_", 5))
		return false;
	if (!SV_ClassnameMatchesList(classname, sv_coop_pickup_targetfix_classes.string))
		return false;

	return SV_CoopWeaponHasTargets(pickup);
}

static void SV_ClearEdictStringField (edict_t *ent, const char *fieldname)
{
	eval_t	*val;

	val = GetEdictFieldValueByName(ent, fieldname);
	if (val)
		val->string = 0;
}

static void SV_ClearCoopWeaponTargets (edict_t *weapon)
{
	SV_ClearEdictStringField(weapon, "target");
	SV_ClearEdictStringField(weapon, "killtarget");
	SV_ClearEdictStringField(weapon, "target2");
	SV_ClearEdictStringField(weapon, "target3");
	SV_ClearEdictStringField(weapon, "target4");
}

static void SV_FireCoopPickupTargets (edict_t *weapon, edict_t *player, const char *reason)
{
	ddef_t		*activator_def;
	dfunction_t	*use_targets;

	if (!SV_CoopWeaponHasTargets(weapon))
		return;

	activator_def = ED_FindGlobal("activator");
	use_targets = ED_FindFunction("SUB_UseTargets");
	if (!activator_def || !use_targets || ((activator_def->type & ~DEF_SAVEGLOBAL) != ev_entity))
		return;

	Con_DPrintf("%s: firing targets for %s\n", reason, PR_GetString(weapon->v.classname));

	pr_global_struct->self = EDICT_TO_PROG(weapon);
	pr_global_struct->other = EDICT_TO_PROG(player);
	pr_global_struct->time = qcvm->time;
	G_INT(activator_def->ofs) = EDICT_TO_PROG(player);
	PR_ExecuteProgram ((func_t)(use_targets - qcvm->functions));

	if (!weapon->free)
		SV_ClearCoopWeaponTargets(weapon);
}

static void SV_LogCoopPickupTargets (edict_t *pickup, edict_t *player, const char *note)
{
	static double	last_log_time;
	const char	*classname;
	const char	*target;
	const char	*killtarget;

	if (!sv_coop_pickup_targetlog.value || !coop.value)
		return;
	if (qcvm->time - last_log_time < 1.0)
		return;
	if (!pickup || pickup->free || !pickup->v.classname)
		return;

	classname = PR_GetString(pickup->v.classname);
	target = PR_GetString(SV_EdictStringFieldValue(pickup, "target"));
	killtarget = PR_GetString(SV_EdictStringFieldValue(pickup, "killtarget"));

	Con_Printf("sv_coop_pickup_targetlog: %s %s after touch by %s target=\"%s\" killtarget=\"%s\"\n",
		classname, note,
		player && player->v.netname ? PR_GetString(player->v.netname) : "client",
		target ? target : "", killtarget ? killtarget : "");
	last_log_time = qcvm->time;
}

// Vanilla-style progs often hide ammo after pickup but only schedule SUB_regen
// in deathmatch. This opt-in coop fix reuses the mod's own respawn function.
static qboolean SV_IsAmmoClassname (const char *classname)
{
	return !q_strcasecmp(classname, "item_shells")
		|| !q_strcasecmp(classname, "item_spikes")
		|| !q_strcasecmp(classname, "item_rockets")
		|| !q_strcasecmp(classname, "item_cells")
		|| !q_strcasecmp(classname, "item_lava_spikes")
		|| !q_strcasecmp(classname, "item_multi_rockets")
		|| !q_strcasecmp(classname, "item_plasma");
}

static qboolean SV_IsCoopAmmoRespawnCandidate (edict_t *ammo, edict_t *player)
{
	if (!sv_coop_ammo_respawn.value || !coop.value)
		return false;
	if (!SV_IsActiveClientEdict(player))
		return false;
	if (!ammo || ammo->free || ammo->v.solid != SOLID_TRIGGER)
		return false;
	if (!ammo->v.classname)
		return false;

	return SV_IsAmmoClassname(PR_GetString(ammo->v.classname));
}

static qboolean SV_IsCoopProgressionItemRespawnCandidate (edict_t *item, edict_t *player)
{
	const char	*classname;

	if (!sv_coop_progression_item_respawn.value || !coop.value)
		return false;
	if (!SV_IsActiveClientEdict(player))
		return false;
	if (!item || item->free || item->v.solid != SOLID_TRIGGER)
		return false;
	if (!item->v.classname)
		return false;

	classname = PR_GetString(item->v.classname);
	return SV_ClassnameMatchesList(classname, sv_coop_progression_item_respawn_classes.string)
		|| q_strcasestr(classname, "suit")
		|| q_strcasestr(classname, "scuba")
		|| q_strcasestr(classname, "diving")
		|| q_strcasestr(classname, "airtank")
		|| q_strcasestr(classname, "air_tank");
}

static qboolean SV_CoopPickupFuncIsNull (func_t func)
{
	dfunction_t	*def;
	func_t		null_func;

	if (!func)
		return true;

	def = ED_FindFunction("SUB_Null");
	null_func = def ? (func_t)(def - qcvm->functions) : 0;

	return null_func && func == null_func;
}

static void SV_ScheduleCoopPickupRespawn (edict_t *pickup, float respawn_time, const char *reason,
	func_t restore_touch, func_t restore_use, qboolean hide_until_respawn)
{
	dfunction_t	*regen_func;

	if (!pickup || pickup->free || pickup->v.solid == SOLID_TRIGGER)
		return;

	regen_func = ED_FindFunction("SUB_regen");
	if (!regen_func)
		return;

	if (respawn_time < 1)
		respawn_time = 1;

	if (restore_touch && SV_CoopPickupFuncIsNull(pickup->v.touch))
		pickup->v.touch = restore_touch;
	if (restore_use && SV_CoopPickupFuncIsNull(pickup->v.use))
		pickup->v.use = restore_use;
	if (hide_until_respawn)
		pickup->v.model = 0;

	pickup->v.think = (func_t)(regen_func - qcvm->functions);
	pickup->v.nextthink = qcvm->time + respawn_time;

	Con_DPrintf("%s: scheduled %s in %.1f seconds\n",
		reason,
		pickup->v.classname ? PR_GetString(pickup->v.classname) : "pickup",
		respawn_time);
}

#define SV_COOP_SHARED_ALL_BITS (-1)
#define SV_COOP_SHARED_DWELL_WEAPON_BITS (4 | 8 | 32)

typedef enum
{
	SV_COOP_SHARED_BITMASK,
	SV_COOP_SHARED_MAXFLOAT
} sv_coop_shared_policy_t;

typedef struct
{
	const char	*name;
	sv_coop_shared_policy_t	policy;
	int		mask;
} sv_coop_shared_field_t;

typedef struct
{
	qboolean	valid;
	int		bits;
	float		value;
} sv_coop_shared_value_t;

#define SV_COOP_SHARED_FIELD_COUNT 19

static const sv_coop_shared_field_t sv_coop_shared_fields[SV_COOP_SHARED_FIELD_COUNT] =
{
	{"items2", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"items3", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"moditems", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"permitems", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"perms", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"customkeys", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"weapons", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"weapon2", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"weapons2", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_ALL_BITS},
	{"items_dwell", SV_COOP_SHARED_BITMASK, SV_COOP_SHARED_DWELL_WEAPON_BITS},
	{"key_count_silver", SV_COOP_SHARED_MAXFLOAT, 0},
	{"key_count_gold", SV_COOP_SHARED_MAXFLOAT, 0},
	{"ammo_shells1", SV_COOP_SHARED_MAXFLOAT, 0},
	{"ammo_nails1", SV_COOP_SHARED_MAXFLOAT, 0},
	{"ammo_lava_nails", SV_COOP_SHARED_MAXFLOAT, 0},
	{"ammo_rockets1", SV_COOP_SHARED_MAXFLOAT, 0},
	{"ammo_multi_rockets", SV_COOP_SHARED_MAXFLOAT, 0},
	{"ammo_cells1", SV_COOP_SHARED_MAXFLOAT, 0},
	{"ammo_plasma", SV_COOP_SHARED_MAXFLOAT, 0}
};

typedef struct
{
	int		items;
	float		ammo_shells;
	float		ammo_nails;
	float		ammo_rockets;
	float		ammo_cells;
	qboolean	worldtype_valid;
	float		worldtype;
	sv_coop_shared_value_t	extra[SV_COOP_SHARED_FIELD_COUNT];
} sv_coop_shared_inventory_t;

#define SV_COOP_SHARED_STOCK_KEY_BITS (IT_KEY1 | IT_KEY2 | IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4)
#define SV_COOP_SHARED_DRAKE_CUSTOM_KEYS (8192 | 16384 | 32768 | 65536)
#define SV_COOP_SHARED_ITEMS2_KEY_BITS 65536
#define SV_COOP_SHARED_WORLD_SILVER_MASK 15
#define SV_COOP_SHARED_WORLD_GOLD_MASK 240
#define SV_COOP_SHARED_WORLD_KEY_MASK (SV_COOP_SHARED_WORLD_SILVER_MASK | SV_COOP_SHARED_WORLD_GOLD_MASK)

static sv_coop_shared_inventory_t sv_coop_shared_touch_before[MAX_SCOREBOARD];
static qboolean sv_coop_shared_touch_valid[MAX_SCOREBOARD];
static int sv_coop_shared_touch_depth[MAX_SCOREBOARD];

void SV_CoopSharedResetClientSlot (int slot)
{
	if (slot < 0 || slot >= MAX_SCOREBOARD)
		return;
	memset(&sv_coop_shared_touch_before[slot], 0,
		sizeof(sv_coop_shared_touch_before[slot]));
	sv_coop_shared_touch_valid[slot] = false;
	sv_coop_shared_touch_depth[slot] = 0;
}

void SV_CoopSharedResetState (void)
{
	int i;
	for (i = 0; i < MAX_SCOREBOARD; ++i)
		SV_CoopSharedResetClientSlot(i);
}

static int SV_CoopSharedItemMask (void)
{
	int	mask;

	/* Only persistent ownership lives in the generic items word.  Armor,
	 * ammo-presence flags and timed powerups are deliberately excluded: their
	 * numeric/timer state belongs to the individual player. */
	mask = IT_SHOTGUN | IT_SUPER_SHOTGUN | IT_NAILGUN | IT_SUPER_NAILGUN |
		IT_GRENADE_LAUNCHER | IT_ROCKET_LAUNCHER | IT_LIGHTNING |
		IT_SUPER_LIGHTNING | IT_AXE | IT_KEY1 | IT_KEY2 |
		IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4;

	if (rogue)
		mask |= RIT_AXE | RIT_LAVA_NAILGUN | RIT_LAVA_SUPER_NAILGUN |
			RIT_MULTI_GRENADE | RIT_MULTI_ROCKET | RIT_PLASMA_GUN;

	if (hipnotic)
		mask |= HIT_PROXIMITY_GUN | HIT_MJOLNIR | HIT_LASER_CANNON;

	return mask;
}

static int SV_CoopSharedStockKeyMask (void)
{
	return SV_COOP_SHARED_STOCK_KEY_BITS;
}

static int SV_CoopSharedExtraKeyMask (const char *name)
{
	if (!name)
		return 0;
	if (!q_strcasecmp(name, "customkeys"))
		return SV_COOP_SHARED_ALL_BITS;
	if (!q_strcasecmp(name, "moditems"))
		return SV_COOP_SHARED_DRAKE_CUSTOM_KEYS;
	if (!q_strcasecmp(name, "items2"))
		return SV_COOP_SHARED_ITEMS2_KEY_BITS;
	return 0;
}

static qboolean SV_CoopSharedIsKeyCountField (const char *name)
{
	return name && (!q_strcasecmp(name, "key_count_silver") ||
		!q_strcasecmp(name, "key_count_gold"));
}

static int SV_CoopSharedClampWorldKeyCount (int count)
{
	if (count < 0)
		return 0;
	if (count > 15)
		return 15;
	return count;
}

static int SV_CoopSharedWorldSilverCount (float worldtype)
{
	return ((int)worldtype) & SV_COOP_SHARED_WORLD_SILVER_MASK;
}

static int SV_CoopSharedWorldGoldCount (float worldtype)
{
	return (((int)worldtype) & SV_COOP_SHARED_WORLD_GOLD_MASK) >> 4;
}

static int SV_CoopSharedWorldWithSilverCount (float worldtype, int count)
{
	int	bits;

	bits = (int)worldtype;
	count = SV_CoopSharedClampWorldKeyCount(count);
	return (bits & ~SV_COOP_SHARED_WORLD_SILVER_MASK) | count;
}

static int SV_CoopSharedWorldWithGoldCount (float worldtype, int count)
{
	int	bits;

	bits = (int)worldtype;
	count = SV_CoopSharedClampWorldKeyCount(count);
	return (bits & ~SV_COOP_SHARED_WORLD_GOLD_MASK) | (count << 4);
}

static qboolean SV_CoopSharedGetWorldType (edict_t *ent, eval_t **val_out, int *type_out)
{
	static dprograms_t	*cached_progs;
	static unsigned short	cached_crc;
	static ddef_t		*cached_def;
	ddef_t	*def;
	int	type;
	eval_t	*val;

	if (!ent || ent->free)
		return false;

	if (cached_progs != qcvm->progs || cached_crc != qcvm->crc)
	{
		cached_progs = qcvm->progs;
		cached_crc = qcvm->crc;
		cached_def = ED_FindField("worldtype");
	}
	def = cached_def;
	if (!def)
		return false;

	type = def->type & ~DEF_SAVEGLOBAL;
	if (type != ev_float && type != ev_ext_integer)
		return false;

	val = GetEdictFieldValue(ent, def->ofs);
	if (!val)
		return false;

	if (val_out)
		*val_out = val;
	if (type_out)
		*type_out = type;
	return true;
}

static void SV_CoopSharedSetWorldTypeValue (eval_t *val, int type, int worldtype)
{
	if (!val)
		return;
	if (type == ev_ext_integer)
		val->_int = worldtype;
	else
		val->_float = (float)worldtype;
}

static void SV_CoopSharedSetWorldKeyCount (edict_t *player, qboolean gold, int count)
{
	eval_t	*val;
	int	type;
	int	worldtype;

	if (!SV_CoopSharedGetWorldType(player, &val, &type))
		return;

	worldtype = gold ? SV_CoopSharedWorldWithGoldCount(type == ev_ext_integer ? (float)val->_int : val->_float, count)
			 : SV_CoopSharedWorldWithSilverCount(type == ev_ext_integer ? (float)val->_int : val->_float, count);
	SV_CoopSharedSetWorldTypeValue(val, type, worldtype);
}

/*
 * QBJ3 stores only the keys *after* the first one in worldtype; the normal
 * IT_KEY bit owns the first key.  Treat worldtype as a counter only for mods
 * which advertise that convention, and never infer key ownership from the
 * extra-count nibble.
 */
static qboolean SV_CoopSharedUsesCountedWorldKeys (void)
{
	static dprograms_t	*cached_progs;
	static unsigned short	cached_crc;
	static qboolean		cached_result;

	if (cached_progs != qcvm->progs || cached_crc != qcvm->crc)
	{
		cached_progs = qcvm->progs;
		cached_crc = qcvm->crc;
		cached_result = ED_FindFunction("key_count_silver") != NULL &&
			ED_FindFunction("key_count_gold") != NULL;
	}
	return cached_result;
}

static void SV_CoopSharedSetExtraBitMask (eval_t *val, int type, int bits)
{
	if (!val)
		return;
	if (type == ev_ext_integer)
		val->_int = bits;
	else
		val->_float = (float)bits;
}

static int SV_CoopSharedGetExtraBitMask (eval_t *val, int type)
{
	if (!val)
		return 0;
	return type == ev_ext_integer ? val->_int : (int)val->_float;
}

static qboolean SV_CoopSharedGetField (edict_t *ent, int index, eval_t **val_out, int *type_out)
{
	static dprograms_t	*cached_progs;
	static unsigned short	cached_crc;
	static ddef_t		*cached_defs[SV_COOP_SHARED_FIELD_COUNT];
	const sv_coop_shared_field_t	*field;
	ddef_t				*def;
	int				i, type;

	if (!ent || ent->free || index < 0 || index >= SV_COOP_SHARED_FIELD_COUNT)
		return false;

	field = &sv_coop_shared_fields[index];
	if (cached_progs != qcvm->progs || cached_crc != qcvm->crc)
	{
		cached_progs = qcvm->progs;
		cached_crc = qcvm->crc;
		for (i = 0; i < SV_COOP_SHARED_FIELD_COUNT; ++i)
			cached_defs[i] = ED_FindField(sv_coop_shared_fields[i].name);
	}
	def = cached_defs[index];
	if (!def)
		return false;

	type = def->type & ~DEF_SAVEGLOBAL;
	if (field->policy == SV_COOP_SHARED_BITMASK)
	{
		if (type != ev_float && type != ev_ext_integer)
			return false;
	}
	else if (type != ev_float)
	{
		return false;
	}

	if (val_out)
		*val_out = GetEdictFieldValue(ent, def->ofs);
	if (type_out)
		*type_out = type;
	return val_out && *val_out;
}

static void SV_CaptureCoopSharedInventory (edict_t *player, sv_coop_shared_inventory_t *inventory)
{
	int	i, type;
	eval_t	*val;

	memset(inventory, 0, sizeof(*inventory));
	if (!player || player->free)
		return;

	inventory->items = (int)player->v.items & SV_CoopSharedItemMask();
	inventory->ammo_shells = player->v.ammo_shells;
	inventory->ammo_nails = player->v.ammo_nails;
	inventory->ammo_rockets = player->v.ammo_rockets;
	inventory->ammo_cells = player->v.ammo_cells;
	if (SV_CoopSharedGetWorldType(player, &val, &type))
	{
		inventory->worldtype_valid = true;
		inventory->worldtype = type == ev_ext_integer ? (float)val->_int : val->_float;
	}

	for (i = 0; i < SV_COOP_SHARED_FIELD_COUNT; i++)
	{
		if (!SV_CoopSharedGetField(player, i, &val, &type))
			continue;

		inventory->extra[i].valid = true;
		if (sv_coop_shared_fields[i].policy == SV_COOP_SHARED_BITMASK)
		{
			if (type == ev_ext_integer)
				inventory->extra[i].bits = val->_int & sv_coop_shared_fields[i].mask;
			else
				inventory->extra[i].bits = (int)val->_float & sv_coop_shared_fields[i].mask;
		}
		else
		{
			inventory->extra[i].value = val->_float;
		}
	}
}

static qboolean SV_IsCoopSharedPickupCandidate (edict_t *pickup, edict_t *player)
{
	if (!coop.value)
		return false;
	if (!SV_IsActiveClientEdict(player))
		return false;
	if (!pickup || pickup->free || pickup->v.solid != SOLID_TRIGGER)
		return false;

	/* Classnames are mod-defined and cannot reliably identify progression.
	 * Snapshot every player-trigger touch and share only a verified inventory
	 * delta after QuakeC accepts it. */
	return true;
}

static qboolean SV_CoopSharedInventoryHasAmmoGain (
	const sv_coop_shared_inventory_t *before,
	const sv_coop_shared_inventory_t *after)
{
	int	i;

	if (after->ammo_shells > before->ammo_shells ||
	    after->ammo_nails > before->ammo_nails ||
	    after->ammo_rockets > before->ammo_rockets ||
	    after->ammo_cells > before->ammo_cells)
		return true;

	for (i = 0; i < SV_COOP_SHARED_FIELD_COUNT; ++i)
	{
		if (sv_coop_shared_fields[i].policy != SV_COOP_SHARED_MAXFLOAT ||
		    SV_CoopSharedIsKeyCountField(sv_coop_shared_fields[i].name) ||
		    !after->extra[i].valid)
			continue;
		if (after->extra[i].value >
		    (before->extra[i].valid ? before->extra[i].value : 0.0f))
			return true;
	}

	return false;
}

static qboolean SV_CoopSharedInventoryHasAcceptedGain (
	const sv_coop_shared_inventory_t *before,
	const sv_coop_shared_inventory_t *after,
	qboolean counted_keys)
{
	int	i;

	if ((after->items & ~before->items) != 0)
		return true;
	if (SV_CoopSharedInventoryHasAmmoGain(before, after))
		return true;
	if (counted_keys && after->worldtype_valid)
	{
		int	before_silver = before->worldtype_valid ? SV_CoopSharedWorldSilverCount(before->worldtype) : 0;
		int	before_gold = before->worldtype_valid ? SV_CoopSharedWorldGoldCount(before->worldtype) : 0;
		if (SV_CoopSharedWorldSilverCount(after->worldtype) > before_silver ||
		    SV_CoopSharedWorldGoldCount(after->worldtype) > before_gold)
			return true;
	}

	for (i = 0; i < SV_COOP_SHARED_FIELD_COUNT; i++)
	{
		if (!after->extra[i].valid)
			continue;
		if (sv_coop_shared_fields[i].policy == SV_COOP_SHARED_BITMASK)
		{
			int	before_bits = before->extra[i].valid ? before->extra[i].bits : 0;
			if ((after->extra[i].bits & ~before_bits) != 0)
				return true;
		}
		else if ((!SV_CoopSharedIsKeyCountField(sv_coop_shared_fields[i].name) ||
			  counted_keys) &&
			 after->extra[i].value >
			 (before->extra[i].valid ? before->extra[i].value : 0.0f))
		{
			/* Extra ammo proves that a duplicate mod weapon was accepted;
			 * counted-key fields prove a quantity pickup.  Neither value is
			 * copied unless its policy below explicitly permits it. */
			return true;
		}
	}

	return false;
}

static qboolean SV_CoopSharedInventoryHasKeyGain (
	const sv_coop_shared_inventory_t *before,
	const sv_coop_shared_inventory_t *after,
	qboolean counted_keys)
{
	int	i;
	int	before_items = before->items;
	int	after_items = after->items;

	if (((after_items & ~before_items) & SV_CoopSharedStockKeyMask()) != 0)
		return true;
	if (counted_keys && after->worldtype_valid)
	{
		int	before_silver = before->worldtype_valid ? SV_CoopSharedWorldSilverCount(before->worldtype) : 0;
		int	before_gold = before->worldtype_valid ? SV_CoopSharedWorldGoldCount(before->worldtype) : 0;

		if (SV_CoopSharedWorldSilverCount(after->worldtype) > before_silver ||
		    SV_CoopSharedWorldGoldCount(after->worldtype) > before_gold)
			return true;
	}

	for (i = 0; i < SV_COOP_SHARED_FIELD_COUNT; ++i)
	{
		int	key_mask = SV_CoopSharedExtraKeyMask(sv_coop_shared_fields[i].name);
		int	before_bits;
		int	gain;

		if (counted_keys &&
		    SV_CoopSharedIsKeyCountField(sv_coop_shared_fields[i].name) &&
		    after->extra[i].valid &&
		    after->extra[i].value >
			(before->extra[i].valid ? before->extra[i].value : 0.0f))
			return true;
		if (!key_mask || !after->extra[i].valid)
			continue;
		before_bits = before->extra[i].valid ? before->extra[i].bits : 0;
		gain = after->extra[i].bits & ~before_bits;
		if (gain & key_mask)
			return true;
	}

	return false;
}

static void SV_CoopSharedApplyInventoryGain (
	edict_t *player,
	const sv_coop_shared_inventory_t *before,
	const sv_coop_shared_inventory_t *after,
	const sv_coop_shared_inventory_t *declared,
	int declared_weapon_bits,
	qboolean share_key_counts,
	qboolean key_gain,
	qboolean direct_weapon_touch)
{
	int	i, type, gain;
	eval_t	*val;
	qboolean	ammo_gain;

	if (!SV_IsActiveClientEdict(player))
		return;

	ammo_gain = SV_CoopSharedInventoryHasAmmoGain(before, after);

	/* Exact QuakeC deltas are authoritative.  A pickup declaration is used
	 * only in a matching accepted domain: ammo growth through the progs'
	 * direct weapon_touch confirms a duplicate weapon, while a key delta/count
	 * confirms a counted key.  This avoids treating unrelated SOLID_TRIGGER
	 * mapper fields as inventory. */
	gain = after->items & ~before->items;
	if (ammo_gain && direct_weapon_touch)
	{
		gain |= declared->items & after->items &
			(SV_CoopSharedItemMask() & ~SV_CoopSharedStockKeyMask());
		/* AD-style pickups declare the granted stock weapon in self.weapon
		 * instead of self.items.  Only trust it after the progs' exact
		 * weapon_touch accepted the pickup and changed ammo, and intersect it
		 * with ownership the source player actually has after the touch. */
		gain |= declared_weapon_bits & after->items &
			(SV_CoopSharedItemMask() & ~SV_CoopSharedStockKeyMask());
	}
	if (key_gain)
		gain |= declared->items & after->items & SV_CoopSharedStockKeyMask();
	if (gain)
		player->v.items = (int)player->v.items | gain;

	if (share_key_counts && after->worldtype_valid)
	{
		int	before_silver = before->worldtype_valid ? SV_CoopSharedWorldSilverCount(before->worldtype) : 0;
		int	before_gold = before->worldtype_valid ? SV_CoopSharedWorldGoldCount(before->worldtype) : 0;
		int	after_silver = SV_CoopSharedWorldSilverCount(after->worldtype);
		int	after_gold = SV_CoopSharedWorldGoldCount(after->worldtype);

		if (after_silver > before_silver)
		{
			eval_t	*world_val;
			int	world_type;
			int	current_silver = 0;

			if (SV_CoopSharedGetWorldType(player, &world_val, &world_type))
				current_silver = SV_CoopSharedWorldSilverCount(world_type == ev_ext_integer ? (float)world_val->_int : world_val->_float);
			SV_CoopSharedSetWorldKeyCount(player, false, q_max(current_silver, after_silver));
		}
		if (after_gold > before_gold)
		{
			eval_t	*world_val;
			int	world_type;
			int	current_gold = 0;

			if (SV_CoopSharedGetWorldType(player, &world_val, &world_type))
				current_gold = SV_CoopSharedWorldGoldCount(world_type == ev_ext_integer ? (float)world_val->_int : world_val->_float);
			SV_CoopSharedSetWorldKeyCount(player, true, q_max(current_gold, after_gold));
		}
	}

	for (i = 0; i < SV_COOP_SHARED_FIELD_COUNT; i++)
	{
		if (!SV_CoopSharedGetField(player, i, &val, &type))
			continue;

		if (sv_coop_shared_fields[i].policy == SV_COOP_SHARED_BITMASK)
		{
			int	before_bits = before->extra[i].valid ? before->extra[i].bits : 0;

			gain = after->extra[i].valid ? (after->extra[i].bits & ~before_bits) : 0;
			if (ammo_gain && direct_weapon_touch && declared->extra[i].valid &&
			    after->extra[i].valid)
				gain |= declared->extra[i].bits & after->extra[i].bits;
			if (key_gain && declared->extra[i].valid && after->extra[i].valid)
				gain |= declared->extra[i].bits & after->extra[i].bits &
					SV_CoopSharedExtraKeyMask(sv_coop_shared_fields[i].name);
			if (!gain)
				continue;

			if (type == ev_ext_integer)
				val->_int = val->_int | gain;
			else
				val->_float = (int)val->_float | gain;
		}
		else
		{
			float	before_value = before->extra[i].valid ? before->extra[i].value : 0.0f;

			if (!after->extra[i].valid || after->extra[i].value <= before_value)
				continue;
			if (SV_CoopSharedIsKeyCountField(sv_coop_shared_fields[i].name) && !share_key_counts)
				continue;
			if (!SV_CoopSharedIsKeyCountField(sv_coop_shared_fields[i].name))
				continue;

			val->_float = q_max(val->_float, after->extra[i].value);
		}
	}
}

static qboolean SV_CoopSharedInventoryHasKeyLoss (
	const sv_coop_shared_inventory_t *before,
	const sv_coop_shared_inventory_t *after)
{
	int	i;

	if (((before->items & ~after->items) & SV_CoopSharedStockKeyMask()) != 0)
		return true;
	if (SV_CoopSharedUsesCountedWorldKeys() && after->worldtype_valid)
	{
		int	before_silver = before->worldtype_valid ? SV_CoopSharedWorldSilverCount(before->worldtype) : 0;
		int	before_gold = before->worldtype_valid ? SV_CoopSharedWorldGoldCount(before->worldtype) : 0;
		if (SV_CoopSharedWorldSilverCount(after->worldtype) < before_silver ||
		    SV_CoopSharedWorldGoldCount(after->worldtype) < before_gold)
			return true;
	}

	for (i = 0; i < SV_COOP_SHARED_FIELD_COUNT; i++)
	{
		if (!before->extra[i].valid || !after->extra[i].valid)
			continue;
		if (sv_coop_shared_fields[i].policy == SV_COOP_SHARED_BITMASK)
		{
			int	key_mask = SV_CoopSharedExtraKeyMask(sv_coop_shared_fields[i].name);
			if (key_mask && ((before->extra[i].bits & ~after->extra[i].bits) & key_mask) != 0)
				return true;
		}
		else if (SV_CoopSharedUsesCountedWorldKeys() &&
			 SV_CoopSharedIsKeyCountField(sv_coop_shared_fields[i].name) &&
			 after->extra[i].value < before->extra[i].value)
		{
			return true;
		}
	}

	return false;
}

static void SV_CoopSharedApplyKeyLoss (
	edict_t *player,
	const sv_coop_shared_inventory_t *before,
	const sv_coop_shared_inventory_t *after)
{
	int	i, type;
	int	lost_items;
	eval_t	*val;

	if (!SV_IsActiveClientEdict(player))
		return;

	lost_items = (before->items & ~after->items) & SV_CoopSharedStockKeyMask();
	if (lost_items)
		player->v.items = (int)player->v.items & ~lost_items;

	if (SV_CoopSharedUsesCountedWorldKeys() && after->worldtype_valid)
	{
		int	before_silver = before->worldtype_valid ? SV_CoopSharedWorldSilverCount(before->worldtype) : 0;
		int	before_gold = before->worldtype_valid ? SV_CoopSharedWorldGoldCount(before->worldtype) : 0;
		int	after_silver = SV_CoopSharedWorldSilverCount(after->worldtype);
		int	after_gold = SV_CoopSharedWorldGoldCount(after->worldtype);

		if (after_silver < before_silver)
			SV_CoopSharedSetWorldKeyCount(player, false, after_silver);
		if (after_gold < before_gold)
			SV_CoopSharedSetWorldKeyCount(player, true, after_gold);
	}

	for (i = 0; i < SV_COOP_SHARED_FIELD_COUNT; i++)
	{
		if (!before->extra[i].valid || !after->extra[i].valid)
			continue;
		if (!SV_CoopSharedGetField(player, i, &val, &type))
			continue;

		if (sv_coop_shared_fields[i].policy == SV_COOP_SHARED_BITMASK)
		{
			int	key_mask = SV_CoopSharedExtraKeyMask(sv_coop_shared_fields[i].name);
			int	lost_bits;
			int	current_bits;

			if (!key_mask)
				continue;
			lost_bits = (before->extra[i].bits & ~after->extra[i].bits) & key_mask;
			if (!lost_bits)
				continue;

			current_bits = SV_CoopSharedGetExtraBitMask(val, type);
			SV_CoopSharedSetExtraBitMask(val, type, current_bits & ~lost_bits);
		}
		else if (SV_CoopSharedUsesCountedWorldKeys() &&
			 SV_CoopSharedIsKeyCountField(sv_coop_shared_fields[i].name) &&
			 after->extra[i].value < before->extra[i].value)
		{
			val->_float = after->extra[i].value;
		}
	}
}

static void SV_SyncCoopSharedKeyLoss (
	edict_t *source,
	const sv_coop_shared_inventory_t *before,
	const sv_coop_shared_inventory_t *after)
{
	int	i;

	if (!coop.value || !SV_IsActiveClientEdict(source))
		return;
	if (!SV_CoopSharedInventoryHasKeyLoss(before, after))
		return;

	for (i = 1; i <= svs.maxclients; i++)
	{
		edict_t	*client = EDICT_NUM(i);
		SV_CoopSharedApplyKeyLoss(client, before, after);
	}

	SV_CoopRespawnSyncSharedKeys(source);
}

static int SV_CoopSharedClientIndex (edict_t *client)
{
	int	num;

	if (!client || client->free || !SV_IsActiveClientEdict(client))
		return -1;

	num = NUM_FOR_EDICT(client);
	if (num < 1 || num > svs.maxclients || num > MAX_SCOREBOARD)
		return -1;
	return num - 1;
}

qboolean SV_CoopSharedBeginClientTouch (edict_t *client)
{
	int	index;

	if (!coop.value)
		return false;

	index = SV_CoopSharedClientIndex(client);
	if (index < 0)
		return false;

	if (sv_coop_shared_touch_depth[index] == 0)
	{
		SV_CaptureCoopSharedInventory(client, &sv_coop_shared_touch_before[index]);
		sv_coop_shared_touch_valid[index] = true;
	}
	sv_coop_shared_touch_depth[index]++;
	return true;
}

void SV_CoopSharedEndClientTouch (edict_t *client)
{
	int	index;
	sv_coop_shared_inventory_t	after;

	index = SV_CoopSharedClientIndex(client);
	if (index < 0)
		return;
	if (!sv_coop_shared_touch_valid[index])
		return;
	if (sv_coop_shared_touch_depth[index] <= 0)
	{
		sv_coop_shared_touch_valid[index] = false;
		return;
	}
	sv_coop_shared_touch_depth[index]--;
	if (sv_coop_shared_touch_depth[index] > 0)
		return;

	SV_CaptureCoopSharedInventory(client, &after);
	SV_SyncCoopSharedKeyLoss(client, &sv_coop_shared_touch_before[index], &after);
	sv_coop_shared_touch_valid[index] = false;
}

static void SV_ShareCoopPickupInventory (
	edict_t *pickup,
	edict_t *source,
	const sv_coop_shared_inventory_t *before,
	const sv_coop_shared_inventory_t *after,
	const sv_coop_shared_inventory_t *declared,
	int declared_weapon_bits,
	qboolean direct_weapon_touch)
{
	int		i;
	const char	*classname;
	qboolean	share_key_counts;
	qboolean	key_gain;

	if (!pickup || !declared)
		return;

	classname = pickup->v.classname ? PR_GetString(pickup->v.classname) : "trigger";
	share_key_counts = SV_CoopSharedUsesCountedWorldKeys();

	if (!SV_CoopSharedInventoryHasAcceptedGain(before, after, share_key_counts))
		return;
	key_gain = SV_CoopSharedInventoryHasKeyGain(before, after,
		share_key_counts);

	for (i = 1; i <= svs.maxclients; i++)
	{
		edict_t	*client = EDICT_NUM(i);

		SV_CoopSharedApplyInventoryGain(client, before, after, declared,
			declared_weapon_bits, share_key_counts, key_gain,
			direct_weapon_touch);
	}

	if (key_gain)
		SV_CoopRespawnSyncSharedKeys(source);

	Con_DPrintf("coop pickup share: %s from %s\n",
		classname,
		source && source->v.netname ? PR_GetString(source->v.netname) : "client");
}


int SV_HullPointContents (hull_t *hull, int num, vec3_t p);

/*
===============================================================================

HULL BOXES

===============================================================================
*/


static	hull_t		box_hull;
static	mclipnode_t	box_clipnodes[6]; //johnfitz -- was dclipnode_t
static	mplane_t	box_planes[6];

/*
===================
SV_InitBoxHull

Set up the planes and clipnodes so that the six floats of a bounding box
can just be stored out and get a proper hull_t structure.
===================
*/
void SV_InitBoxHull (void)
{
	int		i;
	int		side;

	box_hull.clipnodes = box_clipnodes;
	box_hull.planes = box_planes;
	box_hull.firstclipnode = 0;
	box_hull.lastclipnode = 5;

	for (i=0 ; i<6 ; i++)
	{
		box_clipnodes[i].planenum = i;

		side = i&1;

		box_clipnodes[i].children[side] = CONTENTS_EMPTY;
		if (i != 5)
			box_clipnodes[i].children[side^1] = i + 1;
		else
			box_clipnodes[i].children[side^1] = CONTENTS_SOLID;

		box_planes[i].type = i>>1;
		box_planes[i].normal[i>>1] = 1;
	}

}


/*
===================
SV_HullForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
hull_t	*SV_HullForBox (vec3_t mins, vec3_t maxs)
{
	box_planes[0].dist = maxs[0];
	box_planes[1].dist = mins[0];
	box_planes[2].dist = maxs[1];
	box_planes[3].dist = mins[1];
	box_planes[4].dist = maxs[2];
	box_planes[5].dist = mins[2];

	return &box_hull;
}



/*
================
SV_HullForEntity

Returns a hull that can be used for testing or clipping an object of mins/maxs
size.
Offset is filled in to contain the adjustment that must be added to the
testing object's origin to get a point to use with the returned hull.
================
*/
hull_t *SV_HullForEntity (edict_t *ent, vec3_t mins, vec3_t maxs, vec3_t offset)
{
	qmodel_t	*model;
	vec3_t		size;
	vec3_t		hullmins, hullmaxs;
	hull_t		*hull;

// decide which clipping hull to use, based on the size
	if (ent->v.solid == SOLID_BSP)
	{	// explicit hulls in the BSP model
		if (ent->v.movetype != MOVETYPE_PUSH)
			Host_Error ("SOLID_BSP without MOVETYPE_PUSH (%s at %f %f %f)",
				    PR_GetString(ent->v.classname), ent->v.origin[0], ent->v.origin[1], ent->v.origin[2]);

		model = sv.models[ (int)ent->v.modelindex ];

		if (!model || model->type != mod_brush)
			Host_Error ("SOLID_BSP with a non bsp model (%s at %f %f %f)",
				    PR_GetString(ent->v.classname), ent->v.origin[0], ent->v.origin[1], ent->v.origin[2]);

		VectorSubtract (maxs, mins, size);
		if (size[0] < 3)
			hull = &model->hulls[0];
		else if (size[0] <= 32)
			hull = &model->hulls[1];
		else
			hull = &model->hulls[2];

// calculate an offset value to center the origin
		VectorSubtract (hull->clip_mins, mins, offset);
		VectorAdd (offset, ent->v.origin, offset);
	}
	else
	{	// create a temp hull from bounding box sizes

		VectorSubtract (ent->v.mins, maxs, hullmins);
		VectorSubtract (ent->v.maxs, mins, hullmaxs);
		hull = SV_HullForBox (hullmins, hullmaxs);

		VectorCopy (ent->v.origin, offset);
	}


	return hull;
}

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/

typedef struct areanode_s
{
	int		axis;		// -1 = leaf node
	float	dist;
	struct areanode_s	*children[2];
	link_t	trigger_edicts;
	link_t	solid_edicts;
} areanode_t;

#define	AREA_DEPTH	7
#define	AREA_NODES	(2<<AREA_DEPTH)

static	areanode_t	sv_areanodes[AREA_NODES];
static	int			sv_numareanodes;

/*
===============
SV_CreateAreaNode

===============
*/
areanode_t *SV_CreateAreaNode (int depth, vec3_t mins, vec3_t maxs)
{
	areanode_t	*anode;
	vec3_t		size;
	vec3_t		mins1, maxs1, mins2, maxs2;

	anode = &sv_areanodes[sv_numareanodes];
	sv_numareanodes++;

	ClearLink (&anode->trigger_edicts);
	ClearLink (&anode->solid_edicts);

	if (depth == AREA_DEPTH)
	{
		anode->axis = -1;
		anode->children[0] = anode->children[1] = NULL;
		return anode;
	}

	VectorSubtract (maxs, mins, size);
	if (size[0] > size[1])
		anode->axis = 0;
	else
		anode->axis = 1;

	anode->dist = 0.5 * (maxs[anode->axis] + mins[anode->axis]);
	VectorCopy (mins, mins1);
	VectorCopy (mins, mins2);
	VectorCopy (maxs, maxs1);
	VectorCopy (maxs, maxs2);

	maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

	anode->children[0] = SV_CreateAreaNode (depth+1, mins2, maxs2);
	anode->children[1] = SV_CreateAreaNode (depth+1, mins1, maxs1);

	return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld (void)
{
	SV_InitBoxHull ();

	memset (sv_areanodes, 0, sizeof(sv_areanodes));
	sv_numareanodes = 0;
	SV_CreateAreaNode (0, sv.worldmodel->mins, sv.worldmodel->maxs);
}


/*
===============
SV_UnlinkEdict

===============
*/
void SV_UnlinkEdict (edict_t *ent)
{
	if (!ent->area.prev)
		return;		// not linked in anywhere
	RemoveLink (&ent->area);
	ent->area.prev = ent->area.next = NULL;
}


/*
====================
SV_AreaTriggerEdicts

Spike -- just builds a list of entities within the area, rather than walking
them and risking the list getting corrupt.
====================
*/
static void
SV_AreaTriggerEdicts ( edict_t *ent, areanode_t *node, edict_t **list, int *listcount, const int listspace )
{
	link_t		*l, *next;
	edict_t		*touch;

// touch linked edicts
	for (l = node->trigger_edicts.next ; l != &node->trigger_edicts ; l = next)
	{
		next = l->next;
		touch = EDICT_FROM_AREA(l);
		if (touch == ent)
			continue;
		if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER)
			continue;
		if (ent->v.absmin[0] > touch->v.absmax[0]
		|| ent->v.absmin[1] > touch->v.absmax[1]
		|| ent->v.absmin[2] > touch->v.absmax[2]
		|| ent->v.absmax[0] < touch->v.absmin[0]
		|| ent->v.absmax[1] < touch->v.absmin[1]
		|| ent->v.absmax[2] < touch->v.absmin[2] )
			continue;

		if (*listcount == listspace)
			return; // should never happen

		list[*listcount] = touch;
		(*listcount)++;
	}

// recurse down both sides
	if (node->axis == -1)
		return;

	if ( ent->v.absmax[node->axis] > node->dist )
		SV_AreaTriggerEdicts ( ent, node->children[0], list, listcount, listspace );
	if ( ent->v.absmin[node->axis] < node->dist )
		SV_AreaTriggerEdicts ( ent, node->children[1], list, listcount, listspace );
}

/*
====================
SV_TouchLinks

ericw -- copy the touching edicts to an array so we can avoid
iteating the trigger_edicts linked list while calling PR_ExecuteProgram
which could potentially corrupt the list while it's being iterated.
Based on code from Spike.
====================
*/
void SV_TouchLinks (edict_t *ent)
{
	edict_t		**list;
	edict_t		*touch;
	int		old_self, old_other;
	int		i, listcount;
	int		mark;
	qboolean	coop_weapon_targetfix;
	qboolean	coop_pickup_targetfix;
	qboolean	coop_targetlog;
	qboolean	coop_ammo_respawn;
	qboolean	coop_progression_item_respawn;
	qboolean	coop_shared_pickup;
	qboolean	coop_shared_touch_sync;
	qboolean	coop_shared_direct_weapon;
	int		coop_shared_declared_weapon_bits;
	func_t		coop_pickup_touch;
	func_t		coop_pickup_use;
	sv_coop_target_state_t	coop_targets_before;
	sv_coop_shared_inventory_t	coop_shared_before;
	sv_coop_shared_inventory_t	coop_shared_after;
	sv_coop_shared_inventory_t	coop_shared_declared;

	mark = Hunk_LowMark ();
	list = (edict_t **) Hunk_Alloc (qcvm->num_edicts*sizeof(edict_t *));

	listcount = 0;
	SV_AreaTriggerEdicts (ent, sv_areanodes, list, &listcount, qcvm->num_edicts);

	for (i = 0; i < listcount; i++)
	{
		touch = list[i];
	// re-validate in case of PR_ExecuteProgram having side effects that make
	// edicts later in the list no longer touch
		if (touch == ent)
			continue;
		if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER)
			continue;
		if (ent->v.absmin[0] > touch->v.absmax[0]
		|| ent->v.absmin[1] > touch->v.absmax[1]
		|| ent->v.absmin[2] > touch->v.absmax[2]
		|| ent->v.absmax[0] < touch->v.absmin[0]
		|| ent->v.absmax[1] < touch->v.absmin[1]
		|| ent->v.absmax[2] < touch->v.absmin[2] )
			continue;
		if (SV_ShouldSkipRecentTeleportTrigger(touch, ent))
			continue;
		old_self = pr_global_struct->self;
		old_other = pr_global_struct->other;
		coop_weapon_targetfix = SV_IsCoopWeaponTargetFixCandidate(touch, ent);
		coop_pickup_targetfix = SV_IsCoopPickupTargetFixCandidate(touch, ent);
		coop_targetlog = coop.value && sv_coop_pickup_targetlog.value
			&& !touch->free && SV_IsActiveClientEdict(ent)
			&& touch->v.classname && SV_CoopWeaponHasTargets(touch);
			if (coop_weapon_targetfix || coop_pickup_targetfix || coop_targetlog)
				SV_CaptureCoopTargetState(touch, &coop_targets_before);
			else
				memset(&coop_targets_before, 0, sizeof(coop_targets_before));
			coop_ammo_respawn = SV_IsCoopAmmoRespawnCandidate(touch, ent);
			coop_progression_item_respawn = SV_IsCoopProgressionItemRespawnCandidate(touch, ent);
			coop_pickup_touch = touch->v.touch;
			coop_pickup_use = touch->v.use;
			coop_shared_pickup = SV_IsCoopSharedPickupCandidate(touch, ent);
			coop_shared_direct_weapon = coop_shared_pickup &&
				SV_IsDirectWeaponTouch(touch->v.touch);
		if (coop_shared_pickup)
		{
			SV_CaptureCoopSharedInventory(ent, &coop_shared_before);
			SV_CaptureCoopSharedInventory(touch, &coop_shared_declared);
			coop_shared_declared_weapon_bits = (int)touch->v.weapon &
				(SV_CoopSharedItemMask() & ~SV_CoopSharedStockKeyMask());
		}
		coop_shared_touch_sync = SV_CoopSharedBeginClientTouch(ent);

		pr_global_struct->self = EDICT_TO_PROG(touch);
		pr_global_struct->other = EDICT_TO_PROG(ent);
		pr_global_struct->time = qcvm->time;
		PR_ExecuteProgram (touch->v.touch);

		if (coop_shared_pickup)
		{
			SV_CaptureCoopSharedInventory(ent, &coop_shared_after);
			SV_ShareCoopPickupInventory(touch, ent, &coop_shared_before,
				&coop_shared_after, &coop_shared_declared,
				coop_shared_declared_weapon_bits,
				coop_shared_direct_weapon);
		}
			if (coop_shared_touch_sync)
				SV_CoopSharedEndClientTouch(ent);
			if (coop_weapon_targetfix && !touch->free && touch->v.solid == SOLID_TRIGGER
				&& SV_CoopTargetStateUnchanged(touch, &coop_targets_before))
			{
				SV_FireCoopPickupTargets(touch, ent, "sv_coop_weapon_targetfix");
			}
			if (coop_pickup_targetfix && !touch->free && touch->v.solid == SOLID_TRIGGER
				&& SV_CoopTargetStateUnchanged(touch, &coop_targets_before))
			{
				SV_FireCoopPickupTargets(touch, ent, "sv_coop_pickup_targetfix");
			}
			if (coop_targetlog && !touch->free && touch->v.solid == SOLID_TRIGGER
				&& coop_targets_before.has_any
				&& SV_CoopTargetStateUnchanged(touch, &coop_targets_before))
			{
				SV_LogCoopPickupTargets(touch, ent, "still has unchanged targets");
			}
			if (coop_ammo_respawn)
				SV_ScheduleCoopPickupRespawn(touch, sv_coop_ammo_respawn_time.value, "sv_coop_ammo_respawn",
					coop_pickup_touch, coop_pickup_use, true);
			if (coop_progression_item_respawn)
				SV_ScheduleCoopPickupRespawn(touch, sv_coop_progression_item_respawn_time.value, "sv_coop_progression_item_respawn",
					coop_pickup_touch, coop_pickup_use, true);

			pr_global_struct->self = old_self;
			pr_global_struct->other = old_other;
	}

// free hunk-allocated edicts array
	Hunk_FreeToLowMark (mark);
}


/*
===============
SV_FindTouchedLeafs

===============
*/
void SV_FindTouchedLeafs (edict_t *ent, mnode_t *node)
{
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int			sides;
	int			leafnum;

	if (node->contents == CONTENTS_SOLID)
		return;

// add an efrag if the node is a leaf

	if ( node->contents < 0)
	{
		if (ent->num_leafs == MAX_ENT_LEAFS)
			return;

		leaf = (mleaf_t *)node;
		leafnum = leaf - sv.worldmodel->leafs - 1;

		ent->leafnums[ent->num_leafs] = leafnum;
		ent->num_leafs++;
		return;
	}

// NODE_MIXED

	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE(ent->v.absmin, ent->v.absmax, splitplane);

// recurse down the contacted sides
	if (sides & 1)
		SV_FindTouchedLeafs (ent, node->children[0]);

	if (sides & 2)
		SV_FindTouchedLeafs (ent, node->children[1]);
}

/*
===============
SV_BoxInPVS
===============
*/
qboolean SV_BoxInPVS (vec3_t mins, vec3_t maxs, byte *pvs, mnode_t *node)
{
	mplane_t	*splitplane;
	mleaf_t		*leaf;
	int			sides;
	int			leafnum;

	if (node->contents == CONTENTS_SOLID)
		return false;

	if (node->contents < 0)
	{
		leaf = (mleaf_t *)node;
		leafnum = leaf - sv.worldmodel->leafs - 1;
		return pvs[leafnum >> 3] & (1 << (leafnum & 7));
	}

	splitplane = node->plane;
	sides = BOX_ON_PLANE_SIDE(mins, maxs, splitplane);

	if (sides & 1 && SV_BoxInPVS (mins, maxs, pvs, node->children[0]))
		return true;

	if (sides & 2 && SV_BoxInPVS (mins, maxs, pvs, node->children[1]))
		return true;

	return false;
}

/*
===============
SV_LinkEdict

===============
*/
void SV_LinkEdict (edict_t *ent, qboolean touch_triggers)
{
	areanode_t	*node;

	if (ent->area.prev)
		SV_UnlinkEdict (ent);	// unlink from old position

	if (ent == qcvm->edicts)
		return;		// don't add the world

	if (ent->free)
		return;

// set the abs box
	VectorAdd (ent->v.origin, ent->v.mins, ent->v.absmin);
	VectorAdd (ent->v.origin, ent->v.maxs, ent->v.absmax);

//
// to make items easier to pick up and allow them to be grabbed off
// of shelves, the abs sizes are expanded
//
	if ((int)ent->v.flags & FL_ITEM)
	{
		ent->v.absmin[0] -= 15;
		ent->v.absmin[1] -= 15;
		ent->v.absmax[0] += 15;
		ent->v.absmax[1] += 15;
	}
	else
	{	// because movement is clipped an epsilon away from an actual edge,
		// we must fully check even when bounding boxes don't quite touch
		ent->v.absmin[0] -= 1;
		ent->v.absmin[1] -= 1;
		ent->v.absmin[2] -= 1;
		ent->v.absmax[0] += 1;
		ent->v.absmax[1] += 1;
		ent->v.absmax[2] += 1;
	}

// link to PVS leafs
	ent->num_leafs = 0;
	if (ent->v.modelindex)
		SV_FindTouchedLeafs (ent, sv.worldmodel->nodes);

	if (ent->v.solid == SOLID_NOT)
		return;

// find the first node that the ent's box crosses
	node = sv_areanodes;
	while (1)
	{
		if (node->axis == -1)
			break;
		if (ent->v.absmin[node->axis] > node->dist)
			node = node->children[0];
		else if (ent->v.absmax[node->axis] < node->dist)
			node = node->children[1];
		else
			break;		// crosses the node
	}

// link it in

	if (ent->v.solid == SOLID_TRIGGER)
		InsertLinkBefore (&ent->area, &node->trigger_edicts);
	else
		InsertLinkBefore (&ent->area, &node->solid_edicts);

// if touch_triggers, touch all entities at this node and decend for more
	if (touch_triggers)
		SV_TouchLinks ( ent );
}



/*
===============================================================================

POINT TESTING IN HULLS

===============================================================================
*/

/*
==================
SV_HullPointContents

==================
*/
int SV_HullPointContents (hull_t *hull, int num, vec3_t p)
{
	float		d;
	mclipnode_t	*node; //johnfitz -- was dclipnode_t
	mplane_t	*plane;

	while (num >= 0)
	{
		if (num < hull->firstclipnode || num > hull->lastclipnode)
			Sys_Error ("SV_HullPointContents: bad node number");

		node = hull->clipnodes + num;
		plane = hull->planes + node->planenum;

		if (plane->type < 3)
			d = p[plane->type] - plane->dist;
		else
			d = DoublePrecisionDotProduct (plane->normal, p) - plane->dist;
		if (d < 0)
			num = node->children[1];
		else
			num = node->children[0];
	}

	return num;
}


/*
==================
SV_PointContents

==================
*/
int SV_PointContents (vec3_t p)
{
	int		cont;

	cont = SV_HullPointContents (&sv.worldmodel->hulls[0], 0, p);
	if (cont <= CONTENTS_CURRENT_0 && cont >= CONTENTS_CURRENT_DOWN)
		cont = CONTENTS_WATER;
	return cont;
}

int SV_TruePointContents (vec3_t p)
{
	return SV_HullPointContents (&sv.worldmodel->hulls[0], 0, p);
}

//===========================================================================

/*
============
SV_TestEntityPosition

This could be a lot more efficient...
============
*/
edict_t	*SV_TestEntityPosition (edict_t *ent)
{
	trace_t	trace;

	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, 0, ent);

	if (trace.startsolid)
		return trace.ent ? trace.ent : qcvm->edicts;

	return NULL;
}


/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/

/*
==================
SV_RecursiveHullCheck

==================
*/
qboolean SV_RecursiveHullCheck (hull_t *hull, int num, float p1f, float p2f, vec3_t p1, vec3_t p2, trace_t *trace)
{
	mclipnode_t	*node; //johnfitz -- was dclipnode_t
	mplane_t	*plane;
	float		t1, t2;
	float		frac;
	int			i;
	vec3_t		mid;
	int			side;
	float		midf;

// check for empty
	if (num < 0)
	{
		if (num != CONTENTS_SOLID)
		{
			trace->allsolid = false;
			if (num == CONTENTS_EMPTY)
				trace->inopen = true;
			else
				trace->inwater = true;
		}
		else
			trace->startsolid = true;
		return true;		// empty
	}

	if (num < hull->firstclipnode || num > hull->lastclipnode)
		Sys_Error ("SV_RecursiveHullCheck: bad node number");

//
// find the point distances
//
	node = hull->clipnodes + num;
	plane = hull->planes + node->planenum;

	if (plane->type < 3)
	{
		t1 = p1[plane->type] - plane->dist;
		t2 = p2[plane->type] - plane->dist;
	}
	else
	{
		t1 = DoublePrecisionDotProduct (plane->normal, p1) - plane->dist;
		t2 = DoublePrecisionDotProduct (plane->normal, p2) - plane->dist;
	}

#if 1
	if (t1 >= 0 && t2 >= 0)
		return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
	if (t1 < 0 && t2 < 0)
		return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);
#else
	if ( (t1 >= DIST_EPSILON && t2 >= DIST_EPSILON) || (t2 > t1 && t1 >= 0) )
		return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
	if ( (t1 <= -DIST_EPSILON && t2 <= -DIST_EPSILON) || (t2 < t1 && t1 <= 0) )
		return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);
#endif

// put the crosspoint DIST_EPSILON pixels on the near side
	if (t1 < 0)
		frac = (t1 + DIST_EPSILON)/(t1-t2);
	else
		frac = (t1 - DIST_EPSILON)/(t1-t2);
	if (frac < 0)
		frac = 0;
	if (frac > 1)
		frac = 1;

	midf = p1f + (p2f - p1f)*frac;
	for (i=0 ; i<3 ; i++)
		mid[i] = p1[i] + frac*(p2[i] - p1[i]);

	side = (t1 < 0);

// move up to the node
	if (!SV_RecursiveHullCheck (hull, node->children[side], p1f, midf, p1, mid, trace) )
		return false;

#ifdef PARANOID
	if (SV_HullPointContents (sv_hullmodel, mid, node->children[side])
	== CONTENTS_SOLID)
	{
		Con_Printf ("mid PointInHullSolid\n");
		return false;
	}
#endif

	if (SV_HullPointContents (hull, node->children[side^1], mid)
	!= CONTENTS_SOLID)
// go past the node
		return SV_RecursiveHullCheck (hull, node->children[side^1], midf, p2f, mid, p2, trace);

	if (trace->allsolid)
		return false;		// never got out of the solid area

//==================
// the other side of the node is solid, this is the impact point
//==================
	if (!side)
	{
		VectorCopy (plane->normal, trace->plane.normal);
		trace->plane.dist = plane->dist;
	}
	else
	{
		VectorSubtract (vec3_origin, plane->normal, trace->plane.normal);
		trace->plane.dist = -plane->dist;
	}

	while (SV_HullPointContents (hull, hull->firstclipnode, mid)
	== CONTENTS_SOLID)
	{ // shouldn't really happen, but does occasionally
		frac -= 0.1;
		if (frac < 0)
		{
			trace->fraction = midf;
			VectorCopy (mid, trace->endpos);
			Con_DPrintf2 ("backup past 0\n");
			return false;
		}
		midf = p1f + (p2f - p1f)*frac;
		for (i=0 ; i<3 ; i++)
			mid[i] = p1[i] + frac*(p2[i] - p1[i]);
	}

	trace->fraction = midf;
	VectorCopy (mid, trace->endpos);

	return false;
}


/*
==================
SV_ClipMoveToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points
==================
*/
trace_t SV_ClipMoveToEntity (edict_t *ent, vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	trace_t		trace;
	vec3_t		offset;
	vec3_t		start_l, end_l;
	hull_t		*hull;

// fill in a default trace
	memset (&trace, 0, sizeof(trace_t));
	trace.fraction = 1;
	trace.allsolid = true;
	VectorCopy (end, trace.endpos);

// get the clipping hull
	hull = SV_HullForEntity (ent, mins, maxs, offset);

	VectorSubtract (start, offset, start_l);
	VectorSubtract (end, offset, end_l);

// trace a line through the apropriate clipping hull
	SV_RecursiveHullCheck (hull, hull->firstclipnode, 0, 1, start_l, end_l, &trace);

// fix trace up by the offset
	if (trace.fraction != 1)
		VectorAdd (trace.endpos, offset, trace.endpos);

// did we clip the move?
	if (trace.fraction < 1 || trace.startsolid  )
		trace.ent = ent;

	return trace;
}

//===========================================================================

/*
====================
SV_ClipToLinks

Mins and maxs enclose the entire area swept by the move
====================
*/
void SV_ClipToLinks ( areanode_t *node, moveclip_t *clip )
{
	link_t		*l, *next;
	edict_t		*touch;
	trace_t		trace;

// touch linked edicts
	for (l = node->solid_edicts.next ; l != &node->solid_edicts ; l = next)
	{
		next = l->next;
		touch = EDICT_FROM_AREA(l);
		if (touch->v.solid == SOLID_NOT)
			continue;
		if (touch == clip->passedict)
			continue;
		if (touch->v.solid == SOLID_TRIGGER)
			Sys_Error ("Trigger in clipping list");

		if (clip->type == MOVE_NOMONSTERS && touch->v.solid != SOLID_BSP)
			continue;

		if (clip->boxmins[0] > touch->v.absmax[0]
		|| clip->boxmins[1] > touch->v.absmax[1]
		|| clip->boxmins[2] > touch->v.absmax[2]
		|| clip->boxmaxs[0] < touch->v.absmin[0]
		|| clip->boxmaxs[1] < touch->v.absmin[1]
		|| clip->boxmaxs[2] < touch->v.absmin[2] )
			continue;

		if (clip->passedict && clip->passedict->v.size[0] && !touch->v.size[0])
			continue;	// points never interact

	// might intersect, so do an exact clip
		if (clip->trace.allsolid)
			return;
		if (clip->passedict)
		{
		 	if (PROG_TO_EDICT(touch->v.owner) == clip->passedict)
				continue;	// don't clip against own missiles
			if (PROG_TO_EDICT(clip->passedict->v.owner) == touch)
				continue;	// don't clip against owner
		}
		if (SV_ShouldSkipCoopPlayerClip(clip, touch))
			continue;

		if ((int)touch->v.flags & FL_MONSTER)
			trace = SV_ClipMoveToEntity (touch, clip->start, clip->mins2, clip->maxs2, clip->end);
		else
			trace = SV_ClipMoveToEntity (touch, clip->start, clip->mins, clip->maxs, clip->end);
		if (trace.allsolid || trace.startsolid ||
		trace.fraction < clip->trace.fraction)
		{
			trace.ent = touch;
		 	if (clip->trace.startsolid)
			{
				clip->trace = trace;
				clip->trace.startsolid = true;
			}
			else
				clip->trace = trace;
		}
		else if (trace.startsolid)
			clip->trace.startsolid = true;
	}

// recurse down both sides
	if (node->axis == -1)
		return;

	if ( clip->boxmaxs[node->axis] > node->dist )
		SV_ClipToLinks ( node->children[0], clip );
	if ( clip->boxmins[node->axis] < node->dist )
		SV_ClipToLinks ( node->children[1], clip );
}


/*
==================
SV_MoveBounds
==================
*/
void SV_MoveBounds (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, vec3_t boxmins, vec3_t boxmaxs)
{
#if 0
// debug to test against everything
boxmins[0] = boxmins[1] = boxmins[2] = -9999;
boxmaxs[0] = boxmaxs[1] = boxmaxs[2] = 9999;
#else
	int		i;

	for (i=0 ; i<3 ; i++)
	{
		if (end[i] > start[i])
		{
			boxmins[i] = start[i] + mins[i] - 1;
			boxmaxs[i] = end[i] + maxs[i] + 1;
		}
		else
		{
			boxmins[i] = end[i] + mins[i] - 1;
			boxmaxs[i] = start[i] + maxs[i] + 1;
		}
	}
#endif
}

/*
==================
SV_Move
==================
*/
trace_t SV_Move (vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end, int type, edict_t *passedict)
{
	moveclip_t	clip;
	int			i;

	memset ( &clip, 0, sizeof ( moveclip_t ) );

// clip to world
	clip.trace = SV_ClipMoveToEntity ( qcvm->edicts, start, mins, maxs, end );

	clip.start = start;
	clip.end = end;
	clip.mins = mins;
	clip.maxs = maxs;
	clip.type = type;
	clip.passedict = passedict;

	if (type == MOVE_MISSILE)
	{
		for (i=0 ; i<3 ; i++)
		{
			clip.mins2[i] = -15;
			clip.maxs2[i] = 15;
		}
	}
	else
	{
		VectorCopy (mins, clip.mins2);
		VectorCopy (maxs, clip.maxs2);
	}

// create the bounding box of the entire move
	SV_MoveBounds ( start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs );

// clip to entities
	SV_ClipToLinks ( sv_areanodes, &clip );

	return clip.trace;
}
