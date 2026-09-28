/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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
// sv_phys.c

#include "quakedef.h"
#include "pmove.h"
#include "vr_gorilla.h"
#include "vr_weapon_calibration.h"
#include "vr_melee_stock_qc.h"
#include <stdint.h>

/*


pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

cvar_t sv_friction = {"sv_friction", "4", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_stopspeed = {"sv_stopspeed", "100", CVAR_NONE};
cvar_t sv_gravity = {"sv_gravity", "800", CVAR_NOTIFY | CVAR_SERVERINFO};
extern cvar_t sv_maxspeed;
cvar_t sv_maxvelocity = {"sv_maxvelocity", "2000", CVAR_NONE};
cvar_t sv_nofriendlyfire = {"sv_nofriendlyfire", "0", CVAR_NOTIFY | CVAR_SERVERINFO};
cvar_t sv_nostep = {"sv_nostep", "0", CVAR_NONE};
cvar_t sv_freezenonclients = {"sv_freezenonclients", "0", CVAR_NONE};
cvar_t sv_gameplayfix_spawnbeforethinks = {"sv_gameplayfix_spawnbeforethinks", "0", CVAR_NONE};
cvar_t sv_gameplayfix_bouncedownslopes = {"sv_gameplayfix_bouncedownslopes", "1", CVAR_NONE}; // fixes grenades making horrible noises on slopes.
cvar_t sv_fastpushmove = {"sv_fastpushmove", "1", CVAR_NONE};								  // 0=old SV_PushMove processing; 1= faster SV_PushMove, (default)
cvar_t sv_analyticphysics = {"sv_analyticphysics", "1", CVAR_NONE}; // gravity/friction integration matches 72Hz physics at any tick rate

qboolean sv_analyticphysics_frame = true; // sv_analyticphysics latched per SV_Physics, QC can flip the cvar mid-tick

/*
 * Co-op friendly-fire protection is a callback scope, not a replacement
 * damage path. Keep QuakeC's normal damage code and temporarily make every
 * other connected player non-damageable while an attributed callback runs.
 */
typedef struct
{
	edict_t *edict;
	float takedamage;
	qboolean protected;
} sv_friendly_fire_player_t;

static qboolean ff_active;
static qboolean ff_suspended;
static qcvm_t *ff_saved_vm;
static edict_t *ff_saved_edicts;
static dprograms_t *ff_saved_progs;
static float *ff_saved_globals;
static globalvars_t *ff_saved_global_struct;
static client_t *ff_saved_clients;
static int ff_saved_maxclients;
static float ff_saved_teamplay;
static sv_friendly_fire_player_t ff_players[MAX_SCOREBOARD];

static qboolean SV_FriendlyFireServerValid (void)
{
	return qcvm == &sv.qcvm && qcvm->edicts && qcvm->progs && qcvm->globals &&
		pr_global_struct && svs.clients && svs.maxclients > 0 &&
		svs.maxclients <= MAX_SCOREBOARD && qcvm->edict_size > 0 &&
		qcvm->num_edicts > svs.maxclients;
}

static qboolean SV_FriendlyFireEntityNumber (edict_t *ent, int *number)
{
	uintptr_t base, address, extent;
	size_t edict_size;
	int num_edicts;

	if (!ent || !number || !qcvm || !qcvm->edicts || qcvm->edict_size <= 0 ||
		qcvm->num_edicts <= 0)
		return false;
	edict_size = (size_t)qcvm->edict_size;
	num_edicts = qcvm->num_edicts;
	if ((size_t)num_edicts > (size_t)-1 / edict_size)
		return false;
	extent = (uintptr_t)((size_t)num_edicts * edict_size);
	base = (uintptr_t)qcvm->edicts;
	address = (uintptr_t)ent;
	if (extent > (uintptr_t)-1 - base || address < base ||
		address - base >= extent || (address - base) % edict_size)
		return false;
	if ((address - base) / edict_size > 2147483647u)
		return false;
	*number = (int)((address - base) / edict_size);
	return true;
}

static qboolean SV_FriendlyFireOwnerSlot (edict_t *ent, int *owner_slot)
{
	int entnum, owner_offset, slot;

	if (!owner_slot || !SV_FriendlyFireServerValid () ||
		!SV_FriendlyFireEntityNumber (ent, &entnum) || ent->free)
		return false;
	if (entnum > 0 && entnum <= svs.maxclients)
	{
		*owner_slot = entnum;
		return true;
	}

	/* QuakeC edict references are byte offsets into this VM's edict array. */
	owner_offset = ent->v.owner;
	if (owner_offset < 0 || owner_offset % qcvm->edict_size)
		return false;
	slot = owner_offset / qcvm->edict_size;
	if (slot < 1 || slot > svs.maxclients || slot >= qcvm->num_edicts)
		return false;
	*owner_slot = slot;
	return true;
}

static qboolean SV_FriendlyFireVMMatches (void)
{
	return ff_saved_vm && qcvm == ff_saved_vm && qcvm == &sv.qcvm &&
		qcvm->edicts == ff_saved_edicts && qcvm->progs == ff_saved_progs &&
		qcvm->globals == ff_saved_globals &&
		pr_global_struct == ff_saved_global_struct &&
		qcvm->num_edicts > ff_saved_maxclients;
}

static void SV_FriendlyFireClearSnapshot (void)
{
	ff_active = false;
	ff_suspended = false;
	ff_saved_vm = NULL;
	ff_saved_edicts = NULL;
	ff_saved_progs = NULL;
	ff_saved_globals = NULL;
	ff_saved_global_struct = NULL;
	ff_saved_clients = NULL;
	ff_saved_maxclients = 0;
	ff_saved_teamplay = 0;
	memset (ff_players, 0, sizeof (ff_players));
}

static qboolean SV_FriendlyFireSnapshotMatches (void)
{
	return SV_FriendlyFireVMMatches () && svs.clients == ff_saved_clients &&
		svs.maxclients == ff_saved_maxclients;
}

static void SV_FriendlyFireRestore (void)
{
	int i;

	if (!SV_FriendlyFireSnapshotMatches ())
		return;
	/* Preserve a different QuakeC teamplay value written by the callback. */
	if (pr_global_struct->teamplay == 0)
		pr_global_struct->teamplay = ff_saved_teamplay;
	for (i = 0; i < ff_saved_maxclients; i++)
	{
		sv_friendly_fire_player_t *snapshot = &ff_players[i];
		client_t *client = &svs.clients[i];
		edict_t *player = EDICT_NUM (i + 1);

		if (snapshot->protected && player == snapshot->edict &&
			client->active && client->edict == player && !player->free &&
			player->v.takedamage == DAMAGE_NO)
			player->v.takedamage = snapshot->takedamage;
	}
}

qboolean SV_CoopFriendlyFireBegin (edict_t *ent)
{
	int owner_slot, i;

	/* Nested callbacks inherit the first callback's attribution and snapshot. */
	if (ff_active || ff_suspended)
		return false;
	if (!sv_nofriendlyfire.value || !coop.value ||
		!SV_FriendlyFireOwnerSlot (ent, &owner_slot))
		return false;

	ff_saved_vm = qcvm;
	ff_saved_edicts = qcvm->edicts;
	ff_saved_progs = qcvm->progs;
	ff_saved_globals = qcvm->globals;
	ff_saved_global_struct = pr_global_struct;
	ff_saved_clients = svs.clients;
	ff_saved_maxclients = svs.maxclients;
	ff_saved_teamplay = pr_global_struct->teamplay;
	ff_active = true;
	pr_global_struct->teamplay = 0;

	for (i = 0; i < ff_saved_maxclients; i++)
	{
		client_t *client = &svs.clients[i];
		edict_t *player = EDICT_NUM (i + 1);
		sv_friendly_fire_player_t *snapshot = &ff_players[i];

		if (i + 1 == owner_slot || !client->active || player->free ||
			client->edict != player || player->v.takedamage == DAMAGE_NO)
			continue;
		snapshot->edict = player;
		snapshot->takedamage = player->v.takedamage;
		snapshot->protected = true;
		player->v.takedamage = DAMAGE_NO;
	}
	return true;
}

void SV_CoopFriendlyFireEnd (void)
{
	if (!ff_active)
		return;
	if (!ff_suspended)
		SV_FriendlyFireRestore ();
	SV_FriendlyFireClearSnapshot ();
}

qboolean SV_CoopFriendlyFireSuspend (void)
{
	if (!ff_active || ff_suspended)
		return false;
	if (!SV_FriendlyFireSnapshotMatches ())
	{
		SV_FriendlyFireClearSnapshot ();
		return false;
	}
	SV_FriendlyFireRestore ();
	ff_suspended = true;
	return true;
}

void SV_CoopFriendlyFireResume (void)
{
	int i;

	if (!ff_active || !ff_suspended)
		return;
	if (!SV_FriendlyFireSnapshotMatches ())
	{
		SV_FriendlyFireClearSnapshot ();
		return;
	}
	/* Reapply only state ClientDisconnect left at its ordinary value. */
	if (pr_global_struct->teamplay == ff_saved_teamplay)
		pr_global_struct->teamplay = 0;
	for (i = 0; i < ff_saved_maxclients; i++)
	{
		sv_friendly_fire_player_t *snapshot = &ff_players[i];
		client_t *client = &svs.clients[i];
		edict_t *player = EDICT_NUM (i + 1);

		if (snapshot->protected && player == snapshot->edict &&
			client->active && client->edict == player && !player->free &&
			player->v.takedamage == snapshot->takedamage)
			player->v.takedamage = DAMAGE_NO;
	}
	ff_suspended = false;
}

void SV_CoopFriendlyFireReset (void)
{
	if (ff_active)
		SV_CoopFriendlyFireEnd ();
	else
		SV_FriendlyFireClearSnapshot ();
}

/*
 * Co-op dead-player save inventory projection.
 *
 * Cache only typed inventory fields while a player is alive.  A corpse may
 * have had its inventory cleared by mod QC before the save path runs, so the
 * save projection merges the cache with any inventory still present on the
 * corpse.  The serialized edict is a copy; the live corpse is never changed.
 */
#define COOP_RESPAWN_ALL_ITEM_BITS (-1)
#define COOP_RESPAWN_DRAKE_CUSTOM_KEYS (8192 | 16384 | 32768 | 65536)
#define COOP_RESPAWN_DWELL_WEAPON_BITS (4 | 8 | 32)
#define COOP_RESPAWN_STOCK_KEY_BITS \
	(IT_KEY1 | IT_KEY2 | IT_SIGIL1 | IT_SIGIL2 | IT_SIGIL3 | IT_SIGIL4)
#define COOP_RESPAWN_ITEMS2_KEY_BITS 65536
#define COOP_RESPAWN_WORLDTYPE_KEY_MASK 255
#define COOP_RESPAWN_AD_KEEP_MODITEMS                                           \
	(2 | 64 | 128 | 4096 | 131072 | 262144 | 524288 | 1048576 | 2097152 |        \
	 4194304 | COOP_RESPAWN_DRAKE_CUSTOM_KEYS)

typedef enum
{
	COOP_RESPAWN_EXTRA_ITEMS2,
	COOP_RESPAWN_EXTRA_ITEMS3,
	COOP_RESPAWN_EXTRA_MODITEMS,
	COOP_RESPAWN_EXTRA_PERMITEMS,
	COOP_RESPAWN_EXTRA_PERMS,
	COOP_RESPAWN_EXTRA_CUSTOMKEYS,
	COOP_RESPAWN_EXTRA_WEAPONS,
	COOP_RESPAWN_EXTRA_WEAPON2,
	COOP_RESPAWN_EXTRA_WEAPONS2,
	COOP_RESPAWN_EXTRA_ITEMS_DWELL,
	COOP_RESPAWN_EXTRA_ITEMS_MOVEMOD,
	COOP_RESPAWN_EXTRA_RUNESHARD_COU,
	COOP_RESPAWN_EXTRA_CURRENTWEAPON,
	COOP_RESPAWN_EXTRA_WORLDTYPE,
	COOP_RESPAWN_EXTRA_KEY_COUNT_SILVER,
	COOP_RESPAWN_EXTRA_KEY_COUNT_GOLD,
	COOP_RESPAWN_EXTRA_AMMO_SHELLS1,
	COOP_RESPAWN_EXTRA_AMMO_NAILS1,
	COOP_RESPAWN_EXTRA_AMMO_LAVA_NAILS,
	COOP_RESPAWN_EXTRA_AMMO_ROCKETS1,
	COOP_RESPAWN_EXTRA_AMMO_MULTI_ROCKETS,
	COOP_RESPAWN_EXTRA_AMMO_CELLS1,
	COOP_RESPAWN_EXTRA_AMMO_PLASMA,
	COOP_RESPAWN_EXTRA_CAN_ROCKET,
	COOP_RESPAWN_EXTRA_ROCKET_LAUNCHER_MODE,
	COOP_RESPAWN_EXTRA_JBOOTS_GOT,
	COOP_RESPAWN_EXTRA_JBOOTS_PREVLIMIT,
	COOP_RESPAWN_EXTRA_JBOOTS_RECHARGELIMIT,
	COOP_RESPAWN_EXTRA_JBOOTS_SFX,
	COOP_RESPAWN_EXTRA_JBOOTS_AMMO,
	COOP_RESPAWN_EXTRA_JBOOTS_ONGROUND,
	COOP_RESPAWN_EXTRA_JBOOTS_FINISHED,
	COOP_RESPAWN_EXTRA_JBOOTS_TIME,
	COOP_RESPAWN_EXTRA_JUMPBOOTS_FINISHED,
	COOP_RESPAWN_EXTRA_JUMPBOOTS_TIME,
	COOP_RESPAWN_EXTRA_JUMPBOOTS_AIRLVL,
	COOP_RESPAWN_EXTRA_JUMPBOOTS_AIRMAX,
	COOP_RESPAWN_EXTRA_JUMPBOOTS_HEIGHT,
	COOP_RESPAWN_EXTRA_JUMPBOOTS_FORWARD,
	COOP_RESPAWN_EXTRA_KEYNAME,
	COOP_RESPAWN_EXTRA_CKEYNAME1,
	COOP_RESPAWN_EXTRA_CKEYNAME2,
	COOP_RESPAWN_EXTRA_CKEYNAME3,
	COOP_RESPAWN_EXTRA_CKEYNAME4,
	COOP_RESPAWN_EXTRA_CKEYSKIN1,
	COOP_RESPAWN_EXTRA_CKEYSKIN2,
	COOP_RESPAWN_EXTRA_CKEYSKIN3,
	COOP_RESPAWN_EXTRA_CKEYSKIN4,
	COOP_RESPAWN_EXTRA_COUNT
} coop_respawn_extra_field_id_t;

typedef enum
{
	COOP_RESPAWN_EXTRA_BITMASK,
	COOP_RESPAWN_EXTRA_MAXFLOAT,
	COOP_RESPAWN_EXTRA_RESTORE_FLOAT,
	COOP_RESPAWN_EXTRA_STRING
} coop_respawn_extra_policy_t;

typedef struct
{
	const char *name;
	coop_respawn_extra_policy_t policy;
	int mask;
} coop_respawn_extra_field_t;

typedef struct
{
	int items;
	float weapon;
	string_t weaponmodel;
	float currentammo;
	float ammo_shells;
	float ammo_nails;
	float ammo_rockets;
	float ammo_cells;
	qboolean extra_valid[COOP_RESPAWN_EXTRA_COUNT];
	int extra_bits[COOP_RESPAWN_EXTRA_COUNT];
	float extra_value[COOP_RESPAWN_EXTRA_COUNT];
	string_t extra_string[COOP_RESPAWN_EXTRA_COUNT];
} coop_respawn_inventory_t;

static const coop_respawn_extra_field_t coop_respawn_extra_fields[] = {
	{"items2", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"items3", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"moditems", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_AD_KEEP_MODITEMS},
	{"permitems", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"perms", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"customkeys", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"weapons", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"weapon2", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"weapons2", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"items_dwell", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_DWELL_WEAPON_BITS},
	{"items_movemod", COOP_RESPAWN_EXTRA_BITMASK, COOP_RESPAWN_ALL_ITEM_BITS},
	{"runeshard_cou", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"currentweapon", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"worldtype", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"key_count_silver", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"key_count_gold", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"ammo_shells1", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"ammo_nails1", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"ammo_lava_nails", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"ammo_rockets1", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"ammo_multi_rockets", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"ammo_cells1", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"ammo_plasma", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"can_rocket", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"rocket_launcher_mode", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"jboots_got", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"jboots_prevlimit", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"jboots_rechargelimit", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"jboots_sfx", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"jboots_ammo", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"jboots_onground", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"jboots_finished", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"jboots_time", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"jumpboots_finished", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"jumpboots_time", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"jumpboots_airlvl", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"jumpboots_airmax", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"jumpboots_height", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"jumpboots_forward", COOP_RESPAWN_EXTRA_MAXFLOAT, 0},
	{"keyname", COOP_RESPAWN_EXTRA_STRING, 0},
	{"ckeyname1", COOP_RESPAWN_EXTRA_STRING, 0},
	{"ckeyname2", COOP_RESPAWN_EXTRA_STRING, 0},
	{"ckeyname3", COOP_RESPAWN_EXTRA_STRING, 0},
	{"ckeyname4", COOP_RESPAWN_EXTRA_STRING, 0},
	{"ckeyskin1", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"ckeyskin2", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"ckeyskin3", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
	{"ckeyskin4", COOP_RESPAWN_EXTRA_RESTORE_FLOAT, 0},
};

static coop_respawn_inventory_t coop_respawn_last_inventory[MAX_SCOREBOARD];
static qboolean coop_respawn_last_inventory_valid[MAX_SCOREBOARD];
static qboolean coop_shared_frame_started_alive[MAX_SCOREBOARD];
static qboolean coop_shared_frame_death_handled[MAX_SCOREBOARD];

void SV_CoopRespawnInventoryResetClientSlot (int slot)
{
	if (slot < 0 || slot >= MAX_SCOREBOARD)
		return;
	memset (&coop_respawn_last_inventory[slot], 0,
		sizeof (coop_respawn_last_inventory[slot]));
	coop_respawn_last_inventory_valid[slot] = false;
	coop_shared_frame_started_alive[slot] = false;
	coop_shared_frame_death_handled[slot] = false;
}

void SV_CoopRespawnInventoryResetState (void)
{
	int i;
	for (i = 0; i < MAX_SCOREBOARD; i++)
		SV_CoopRespawnInventoryResetClientSlot (i);
}

static qboolean SV_CoopIsActiveClient (edict_t *ent)
{
	int entnum;
	if (!ent || ent->free)
		return false;
	entnum = NUM_FOR_EDICT (ent);
	return entnum >= 1 && entnum <= svs.maxclients &&
		svs.clients[entnum - 1].active && svs.clients[entnum - 1].spawned;
}

static qboolean SV_CoopIsDeadClient (edict_t *ent)
{
	return SV_CoopIsActiveClient (ent) &&
		(ent->v.health <= 0 || ent->v.deadflag >= DEAD_DYING);
}

static qboolean SV_CoopRespawnIsAliveClient (edict_t *ent)
{
	return SV_CoopIsActiveClient (ent) && ent->v.health > 0 &&
		ent->v.deadflag == DEAD_NO && ent->v.solid != SOLID_NOT;
}

static float SV_CoopRespawnMaxFloat (float a, float b)
{
	return a > b ? a : b;
}

static float SV_CoopRespawnCurrentAmmoForWeapon (edict_t *ent, float weapon,
	float fallback)
{
	int weapon_item = (int)weapon;
	if (weapon_item == IT_SHOTGUN || weapon_item == IT_SUPER_SHOTGUN)
		return ent->v.ammo_shells;
	if (weapon_item == IT_NAILGUN || weapon_item == IT_SUPER_NAILGUN ||
		(rogue && (weapon_item == RIT_LAVA_NAILGUN ||
		RIT_LAVA_SUPER_NAILGUN == weapon_item)))
		return ent->v.ammo_nails;
	if (weapon_item == IT_GRENADE_LAUNCHER ||
		weapon_item == IT_ROCKET_LAUNCHER ||
		(rogue && (weapon_item == RIT_MULTI_GRENADE ||
		weapon_item == RIT_MULTI_ROCKET)) ||
		(hipnotic && weapon_item == HIT_PROXIMITY_GUN))
		return ent->v.ammo_rockets;
	if (weapon_item == IT_LIGHTNING ||
		(hipnotic && (weapon_item == HIT_LASER_CANNON ||
		weapon_item == HIT_MJOLNIR)) ||
		(rogue && weapon_item == RIT_PLASMA_GUN))
		return ent->v.ammo_cells;
	return fallback;
}

static int SV_CoopRespawnKeepItemMask (void)
{
	int mask;
	mask = IT_SHOTGUN | IT_SUPER_SHOTGUN | IT_NAILGUN | IT_SUPER_NAILGUN |
		IT_GRENADE_LAUNCHER | IT_ROCKET_LAUNCHER | IT_LIGHTNING |
		IT_SUPER_LIGHTNING | IT_AXE | IT_SHELLS | IT_NAILS | IT_ROCKETS |
		IT_CELLS | IT_KEY1 | IT_KEY2 | IT_SIGIL1 | IT_SIGIL2 |
		IT_SIGIL3 | IT_SIGIL4;
	mask |= SV_DeclaredWeaponBits ();
	if (rogue)
		mask |= RIT_AXE | RIT_LAVA_NAILGUN | RIT_LAVA_SUPER_NAILGUN |
			RIT_MULTI_GRENADE | RIT_MULTI_ROCKET | RIT_PLASMA_GUN |
			RIT_SHELLS | RIT_NAILS | RIT_ROCKETS | RIT_CELLS |
			RIT_LAVA_NAILS | RIT_PLASMA_AMMO | RIT_MULTI_ROCKETS;
	if (hipnotic)
		mask |= HIT_PROXIMITY_GUN | HIT_MJOLNIR | HIT_LASER_CANNON;
	return mask;
}

static eval_t *SV_CoopRespawnGetExtraField (edict_t *ent, int index,
	int *type_out)
{
	const coop_respawn_extra_field_t *field;
	ddef_t *def;
	int type;
	if (!ent || ent->free || index < 0 || index >= COOP_RESPAWN_EXTRA_COUNT)
		return NULL;
	field = &coop_respawn_extra_fields[index];
	def = ED_FindField (field->name);
	if (!def)
		return NULL;
	type = def->type & ~DEF_SAVEGLOBAL;
	if (field->policy == COOP_RESPAWN_EXTRA_STRING)
	{
		if (type != ev_string)
			return NULL;
	}
	else if (field->policy == COOP_RESPAWN_EXTRA_BITMASK)
	{
		if (type != ev_float && type != ev_ext_integer)
			return NULL;
	}
	else if (type != ev_float)
		return NULL;
	if (type_out)
		*type_out = type;
	return GetEdictFieldValue (ent, def->ofs);
}

static int SV_CoopRespawnExtraSharedKeyMask (const char *name)
{
	if (!name)
		return 0;
	if (!q_strcasecmp (name, "customkeys"))
		return COOP_RESPAWN_ALL_ITEM_BITS;
	if (!q_strcasecmp (name, "moditems"))
		return COOP_RESPAWN_DRAKE_CUSTOM_KEYS;
	if (!q_strcasecmp (name, "items2"))
		return COOP_RESPAWN_ITEMS2_KEY_BITS;
	return 0;
}

static qboolean SV_CoopRespawnExtraIsSharedKeyCount (const char *name)
{
	return name && (!q_strcasecmp (name, "key_count_silver") ||
		!q_strcasecmp (name, "key_count_gold"));
}

static qboolean SV_CoopRespawnExtraIsKeyMetadata (const char *name)
{
	return name && (!q_strncasecmp (name, "ckeyname", 8) ||
		!q_strncasecmp (name, "ckeyskin", 8));
}

void SV_CoopRespawnSyncSharedKeys (edict_t *source)
{
	int i, j;
	int source_items;
	qboolean counted_keys;
	if (!coop.value || !source || source->free)
		return;
	source_items = (int)source->v.items & COOP_RESPAWN_STOCK_KEY_BITS;
	counted_keys = SV_CoopUsesCountedKeys ();
	for (i = 0; i < MAX_SCOREBOARD; i++)
	{
		coop_respawn_inventory_t *inventory;
		if (!coop_respawn_last_inventory_valid[i])
			continue;
		inventory = &coop_respawn_last_inventory[i];
		inventory->items = (inventory->items & ~COOP_RESPAWN_STOCK_KEY_BITS) |
			source_items;
		for (j = 0; j < COOP_RESPAWN_EXTRA_COUNT; j++)
		{
			const coop_respawn_extra_field_t *field =
				&coop_respawn_extra_fields[j];
			eval_t *val;
			int type;
			val = SV_CoopRespawnGetExtraField (source, j, &type);
			if (!val)
				continue;
			if (SV_CoopRespawnExtraIsKeyMetadata (field->name))
			{
				inventory->extra_valid[j] = true;
				if (field->policy == COOP_RESPAWN_EXTRA_STRING)
					inventory->extra_string[j] = val->string;
				else
					inventory->extra_value[j] = val->_float;
			}
			else if (field->policy == COOP_RESPAWN_EXTRA_BITMASK)
			{
				int key_mask = SV_CoopRespawnExtraSharedKeyMask (field->name);
				int source_bits;
				if (!key_mask)
					continue;
				source_bits = type == ev_ext_integer ? val->_int :
					(int)val->_float;
				inventory->extra_valid[j] = true;
				inventory->extra_bits[j] =
					(inventory->extra_bits[j] & ~key_mask) |
					(source_bits & key_mask);
			}
			else if (counted_keys &&
				SV_CoopRespawnExtraIsSharedKeyCount (field->name))
			{
				inventory->extra_valid[j] = true;
				inventory->extra_value[j] = val->_float;
			}
			else if (counted_keys && !q_strcasecmp (field->name, "worldtype"))
			{
				int source_bits = type == ev_ext_integer ? val->_int :
					(int)val->_float;
				int existing_bits = inventory->extra_valid[j] ?
					(int)inventory->extra_value[j] : 0;
				inventory->extra_valid[j] = true;
				inventory->extra_value[j] = (float)((existing_bits &
					~COOP_RESPAWN_WORLDTYPE_KEY_MASK) |
					(source_bits & COOP_RESPAWN_WORLDTYPE_KEY_MASK));
			}
		}
	}
}

static void SV_CoopRespawnSaveInventory (edict_t *ent,
	coop_respawn_inventory_t *inventory)
{
	int i, type;
	eval_t *val;
	memset (inventory, 0, sizeof (*inventory));
	inventory->items = (int)ent->v.items & SV_CoopRespawnKeepItemMask ();
	inventory->weapon = ent->v.weapon;
	inventory->weaponmodel = ent->v.weaponmodel;
	inventory->currentammo = ent->v.currentammo;
	inventory->ammo_shells = ent->v.ammo_shells;
	inventory->ammo_nails = ent->v.ammo_nails;
	inventory->ammo_rockets = ent->v.ammo_rockets;
	inventory->ammo_cells = ent->v.ammo_cells;
	for (i = 0; i < COOP_RESPAWN_EXTRA_COUNT; i++)
	{
		val = SV_CoopRespawnGetExtraField (ent, i, &type);
		if (!val)
			continue;
		if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_STRING)
		{
			if (val->string && PR_GetString (val->string)[0])
			{
				inventory->extra_valid[i] = true;
				inventory->extra_string[i] = val->string;
			}
		}
		else
		{
			inventory->extra_valid[i] = true;
			if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_BITMASK)
			{
				if (type == ev_ext_integer)
					inventory->extra_bits[i] = val->_int & coop_respawn_extra_fields[i].mask;
				else
					inventory->extra_bits[i] = (int)val->_float &
						coop_respawn_extra_fields[i].mask;
			}
			else
				inventory->extra_value[i] = val->_float;
		}
	}
}

static void SV_CoopRespawnMergeInventory (coop_respawn_inventory_t *dst,
	const coop_respawn_inventory_t *src)
{
	int i;
	dst->items |= src->items;
	dst->ammo_shells = SV_CoopRespawnMaxFloat (dst->ammo_shells, src->ammo_shells);
	dst->ammo_nails = SV_CoopRespawnMaxFloat (dst->ammo_nails, src->ammo_nails);
	dst->ammo_rockets = SV_CoopRespawnMaxFloat (dst->ammo_rockets, src->ammo_rockets);
	dst->ammo_cells = SV_CoopRespawnMaxFloat (dst->ammo_cells, src->ammo_cells);
	dst->currentammo = SV_CoopRespawnMaxFloat (dst->currentammo, src->currentammo);
	if (dst->weapon <= 0 && src->weapon > 0)
		dst->weapon = src->weapon;
	if (!dst->weaponmodel && src->weaponmodel)
		dst->weaponmodel = src->weaponmodel;
	for (i = 0; i < COOP_RESPAWN_EXTRA_COUNT; i++)
	{
		if (!src->extra_valid[i])
			continue;
		if (!dst->extra_valid[i])
		{
			dst->extra_valid[i] = true;
			dst->extra_bits[i] = src->extra_bits[i];
			dst->extra_value[i] = src->extra_value[i];
			dst->extra_string[i] = src->extra_string[i];
			continue;
		}
		if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_BITMASK)
			dst->extra_bits[i] |= src->extra_bits[i];
		else if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_STRING)
		{
			if (!dst->extra_string[i] && src->extra_string[i])
				dst->extra_string[i] = src->extra_string[i];
		}
		else if (coop_respawn_extra_fields[i].policy != COOP_RESPAWN_EXTRA_RESTORE_FLOAT)
			dst->extra_value[i] = SV_CoopRespawnMaxFloat (dst->extra_value[i],
				src->extra_value[i]);
	}
}

static void SV_CoopRespawnRestoreInventory (edict_t *ent,
	const coop_respawn_inventory_t *inventory)
{
	int i, type;
	eval_t *val;
	ent->v.items = (int)ent->v.items | inventory->items;
	ent->v.ammo_shells = SV_CoopRespawnMaxFloat (ent->v.ammo_shells,
		inventory->ammo_shells);
	ent->v.ammo_nails = SV_CoopRespawnMaxFloat (ent->v.ammo_nails,
		inventory->ammo_nails);
	ent->v.ammo_rockets = SV_CoopRespawnMaxFloat (ent->v.ammo_rockets,
		inventory->ammo_rockets);
	ent->v.ammo_cells = SV_CoopRespawnMaxFloat (ent->v.ammo_cells,
		inventory->ammo_cells);
	if (inventory->weapon > 0)
		ent->v.weapon = inventory->weapon;
	if (inventory->weaponmodel)
		ent->v.weaponmodel = inventory->weaponmodel;
	for (i = 0; i < COOP_RESPAWN_EXTRA_COUNT; i++)
	{
		if (!inventory->extra_valid[i])
			continue;
		val = SV_CoopRespawnGetExtraField (ent, i, &type);
		if (!val)
			continue;
		if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_STRING)
			val->string = inventory->extra_string[i];
		else if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_BITMASK)
		{
			if (type == ev_ext_integer)
				val->_int |= inventory->extra_bits[i];
			else
				val->_float = (int)val->_float | inventory->extra_bits[i];
		}
		else if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_RESTORE_FLOAT)
			val->_float = inventory->extra_value[i];
		else
			val->_float = SV_CoopRespawnMaxFloat (val->_float,
				inventory->extra_value[i]);
	}
	if (inventory->weapon > 0)
		ent->v.currentammo = SV_CoopRespawnCurrentAmmoForWeapon (ent,
			inventory->weapon, inventory->currentammo);
	else
		ent->v.currentammo = SV_CoopRespawnMaxFloat (ent->v.currentammo,
			inventory->currentammo);
}

/* Restore the serialized typed inventory over the fresh QC player state.
 * Exact assignment preserves saved zero ammo and leaves unrelated fresh QC
 * fields, callbacks, health, movement, and references intact. */
static void SV_CoopRespawnRestoreSavedInventoryExact (edict_t *ent,
	const coop_respawn_inventory_t *inventory)
{
	int i, type, mask;
	eval_t *val;
	float fresh_weapon = ent->v.weapon;
	string_t fresh_weaponmodel = ent->v.weaponmodel;
	string_t weaponmodel = inventory->weaponmodel;

	/* A legacy projection may have a selected owned weapon but no model. Reuse
	 * the new spawn model only when it belongs to that same selected weapon. */
	if (inventory->weapon > 0 && (inventory->items & (int)inventory->weapon) &&
		!weaponmodel && fresh_weapon == inventory->weapon && fresh_weaponmodel)
		weaponmodel = fresh_weaponmodel;

	mask = SV_CoopRespawnKeepItemMask ();
	ent->v.items = ((int)ent->v.items & ~mask) | (inventory->items & mask);
	ent->v.ammo_shells = inventory->ammo_shells;
	ent->v.ammo_nails = inventory->ammo_nails;
	ent->v.ammo_rockets = inventory->ammo_rockets;
	ent->v.ammo_cells = inventory->ammo_cells;
	ent->v.currentammo = inventory->currentammo;
	ent->v.weapon = inventory->weapon;
	ent->v.weaponmodel = weaponmodel;
	for (i = 0; i < COOP_RESPAWN_EXTRA_COUNT; i++)
	{
		if (!inventory->extra_valid[i])
			continue;
		val = SV_CoopRespawnGetExtraField (ent, i, &type);
		if (!val)
			continue;
		if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_STRING)
			val->string = inventory->extra_string[i];
		else if (coop_respawn_extra_fields[i].policy == COOP_RESPAWN_EXTRA_BITMASK)
		{
			mask = coop_respawn_extra_fields[i].mask;
			if (type == ev_ext_integer)
				val->_int = (val->_int & ~mask) | (inventory->extra_bits[i] & mask);
			else
				val->_float = ((int)val->_float & ~mask) |
					(inventory->extra_bits[i] & mask);
		}
		else
			val->_float = inventory->extra_value[i];
	}
}

static void SV_CoopRespawnRememberAliveInventory (edict_t *ent, int num)
{
	int index;
	if (!coop.value || !SV_CoopRespawnIsAliveClient (ent))
		return;
	index = num - 1;
	if (index < 0 || index >= MAX_SCOREBOARD)
		return;
	SV_CoopRespawnSaveInventory (ent, &coop_respawn_last_inventory[index]);
	coop_respawn_last_inventory_valid[index] = true;
}

void SV_CoopRespawnRefreshClientInventory (edict_t *ent)
{
	int num;
	if (!ent || ent->free)
		return;
	num = NUM_FOR_EDICT (ent);
	if (num < 1 || num > svs.maxclients)
		return;
	SV_CoopRespawnRememberAliveInventory (ent, num);
}

static void SV_CoopSharedBeginFrameDeathTracking (void)
{
	int i;

	if (qcvm != &sv.qcvm)
		return;
	memset (coop_shared_frame_started_alive, 0,
		sizeof (coop_shared_frame_started_alive));
	memset (coop_shared_frame_death_handled, 0,
		sizeof (coop_shared_frame_death_handled));
	if (!coop.value)
		return;

	for (i = 1; i <= svs.maxclients && i <= MAX_SCOREBOARD; i++)
		if (SV_CoopRespawnIsAliveClient (EDICT_NUM (i)))
			coop_shared_frame_started_alive[i - 1] = true;
}

static void SV_CoopSharedObserveClientDeath (edict_t *ent, int num)
{
	int index = num - 1;

	if (qcvm != &sv.qcvm || !coop.value || !ent || ent->free ||
		index < 0 || index >= MAX_SCOREBOARD ||
		!coop_shared_frame_started_alive[index] ||
		coop_shared_frame_death_handled[index] || !SV_CoopIsDeadClient (ent))
		return;

	/* Mark first so nested QC or later frame scans cannot reconcile twice. */
	coop_shared_frame_death_handled[index] = true;
	SV_CoopSharedReconcileClientDeath (ent);
}

static void SV_CoopSharedEndFrameDeathTracking (void)
{
	int i;

	if (qcvm != &sv.qcvm || !coop.value)
		return;
	for (i = 1; i <= svs.maxclients && i <= MAX_SCOREBOARD; i++)
		if (coop_shared_frame_started_alive[i - 1] &&
			!coop_shared_frame_death_handled[i - 1])
			SV_CoopSharedObserveClientDeath (EDICT_NUM (i), i);
}

void SV_CoopRespawnSaveClientEdict (edict_t *ent, edict_t *snapshot)
{
	int index;
	coop_respawn_inventory_t inventory, current;
	if (!ent || !snapshot || ent->free || !qcvm)
		return;
	/* Projection is serialization-only: never revive or mutate the corpse. */
	memcpy (snapshot, ent, qcvm->edict_size);
	index = NUM_FOR_EDICT (ent) - 1;
	if (!coop.value ||
		!SV_CoopFeatureEnabled (&sv_coop_respawn_keep_weapons_ammo, true) ||
		!SV_CoopIsDeadClient (ent) || index < 0 || index >= MAX_SCOREBOARD ||
		!coop_respawn_last_inventory_valid[index])
		return;
	inventory = coop_respawn_last_inventory[index];
	SV_CoopRespawnSaveInventory (ent, &current);
	SV_CoopRespawnMergeInventory (&inventory, &current);
	SV_CoopRespawnRestoreInventory (snapshot, &inventory);
	/* The respawn helper maps currentammo to reserve ammo; serialization keeps
	 * the cached magazine value exactly. */
	snapshot->v.currentammo = inventory.currentammo;
}

void SV_CoopRespawnRestoreSavedInventory (edict_t *ent, edict_t *snapshot)
{
	coop_respawn_inventory_t inventory;
	if (!ent || ent->free || !snapshot || snapshot->free)
		return;
	if (coop.value &&
		SV_CoopFeatureEnabled (&sv_coop_respawn_keep_weapons_ammo, true))
	{
		SV_CoopRespawnSaveInventory (snapshot, &inventory);
		SV_CoopRespawnRestoreSavedInventoryExact (ent, &inventory);
	}
	/* Team keys are restored even when optional weapon retention is disabled. */
	SV_CoopSharedApplyToJoiningClient (ent);
}

#define MOVE_EPSILON 0.01

// max depth float rounding can embed an entity into the surface it rests on, anything deeper is a real overlap
#define PUSH_CONTACT_EPSILON (2 * DIST_EPSILON)
// deliberately wider than the attach epsilon: contact must clear decisively
// before a carried entity is released, or rounding makes riders chatter
#define PUSH_RELEASE_EPSILON 1.0f
#define MIN_WALK_NORMAL		 0.7f
#define STEPSIZE			 18

static void		SV_Physics_Toss (edict_t *ent, qboolean think_already_ran);
static edict_t *sv_walk_support_pusher;
static vec3_t	sv_walk_support_normal;

// For usage by SV_PushMove, allocate at max possible size,
// fine to be static because all SV_phys is only called from the main thread.
static edict_t *pushable_ent_cache[MAX_EDICTS];
static int		num_pushable_ent_cache;

// Spatial hash over the pushable cache so each moving pusher only tests nearby
// entities instead of scanning the whole cache. Rebuilt once per SV_Physics.
// Entities that move during the tick are reinserted at their new position by
// SV_LinkEdict, so entries only need to cover linked positions. Entities
// allocated mid-tick (cache entries at index >= push_grid_tail_start) and
// oversized entities bypass the grid and are always tested.
#define PUSH_GRID_CELL_SHIFT 8 // 256 unit cells
#define PUSH_GRID_MAX_LARGE	 1024
#define PUSH_GRID_MAX_QUERY_CELLS 4096

typedef struct
{
	edict_t *ent;
	int		 next;
} push_grid_entry_t;

static hash_map_t		 *push_grid_map; // cell coords -> head index into push_grid_entries
static push_grid_entry_t *push_grid_entries;
static int				  push_grid_entries_capacity;
static int				  push_grid_num_entries;
static edict_t			 *push_grid_large[PUSH_GRID_MAX_LARGE];
static int				  push_grid_num_large;
static int				  push_grid_tail_start;
static qboolean			  push_grid_valid;
static qboolean			  push_grid_active; // inside SV_Physics with a built grid
static qcvm_t			 *push_grid_qcvm;	// vm the grid was built for, entities from other vms must not mix in

static qboolean SV_IsPushable (edict_t *ent)
{
	return ent->v.movetype != MOVETYPE_PUSH && ent->v.movetype != MOVETYPE_NONE && ent->v.movetype != MOVETYPE_NOCLIP;
}

typedef struct
{
	int32_t x, y, z;
} push_grid_cell_t;

static uint32_t PushGrid_HashCell (const void *const val)
{
	const push_grid_cell_t *cell = (const push_grid_cell_t *)val;
	return HashCombine (HashInt32 (&cell->x), HashCombine (HashInt32 (&cell->y), HashInt32 (&cell->z)));
}

static int PushGrid_Cell (float v)
{
	// garbage origins (NaN, huge floats from broken QC) must not reach the int conversion
	const float limit = 1 << 23;
	if (!(v >= -limit)) // also catches NaN
		v = -limit;
	else if (v > limit)
		v = limit;
	return ((int)floorf (v)) >> PUSH_GRID_CELL_SHIFT;
}

static void PushGrid_CellRange (const vec3_t absmin, const vec3_t absmax, float inflate, int lo[3], int hi[3])
{
	for (int i = 0; i < 3; i++)
	{
		lo[i] = PushGrid_Cell (absmin[i] - inflate);
		hi[i] = PushGrid_Cell (absmax[i] + inflate);
	}
}

static void PushGrid_Clear (void)
{
	if (!push_grid_map)
		push_grid_map = HashMap_Create (push_grid_cell_t, int32_t, &PushGrid_HashCell, NULL);
	HashMap_Clear (push_grid_map);
	push_grid_num_entries = 0;
	push_grid_num_large = 0;
	push_grid_valid = true;
	push_grid_active = false;
}

static void PushGrid_Insert (edict_t *ent)
{
	int lo[3], hi[3];
	PushGrid_CellRange (ent->v.absmin, ent->v.absmax, 0.0f, lo, hi);

	// per-axis span check before the multiply so huge boxes can't overflow the cell count
	if (hi[0] - lo[0] >= 4 || hi[1] - lo[1] >= 4 || hi[2] - lo[2] >= 4)
	{
		if (push_grid_num_large == PUSH_GRID_MAX_LARGE)
			push_grid_valid = false;
		else
			push_grid_large[push_grid_num_large++] = ent;
		return;
	}

	int cells = (hi[0] - lo[0] + 1) * (hi[1] - lo[1] + 1) * (hi[2] - lo[2] + 1);

	if (push_grid_num_entries + cells > push_grid_entries_capacity)
	{
		push_grid_entries_capacity = q_max (push_grid_entries_capacity * 2, 4096);
		push_grid_entries = Mem_Realloc (push_grid_entries, push_grid_entries_capacity * sizeof (push_grid_entry_t));
	}

	for (int x = lo[0]; x <= hi[0]; x++)
		for (int y = lo[1]; y <= hi[1]; y++)
			for (int z = lo[2]; z <= hi[2]; z++)
			{
				push_grid_cell_t key = {x, y, z};
				int32_t			 index = push_grid_num_entries;
				int32_t			*head = HashMap_Lookup (int32_t, push_grid_map, &key);

				push_grid_entries[index].ent = ent;
				push_grid_entries[index].next = head ? *head : -1;
				if (head)
					*head = index;
				else
					HashMap_Insert (push_grid_map, &key, &index);
				push_grid_num_entries++;
			}
}

static int PushGrid_CompareEdictNumbers (edict_t *a, edict_t *b)
{
	/* Grid/cache entries already belong to this QCVM; sorting needs only their
	 * contiguous edict-array positions, without checked function calls. */
	const int a_num = NUM_FOR_EDICT_NO_CHECK (a);
	const int b_num = NUM_FOR_EDICT_NO_CHECK (b);

	return (a_num > b_num) - (a_num < b_num);
}

static void PushGrid_SiftDown (edict_t **out, int root, int count)
{
	edict_t *key = out[root];
	int		 child;

	while ((child = root * 2 + 1) < count)
	{
		if (child + 1 < count &&
			PushGrid_CompareEdictNumbers (out[child], out[child + 1]) < 0)
			child++;
		if (PushGrid_CompareEdictNumbers (key, out[child]) >= 0)
			break;
		out[root] = out[child];
		root = child;
	}
	out[root] = key;
}

/*
============
SV_PushGridEntityLinked

Called from SV_LinkEdict. All absbox changes pass through there, so
re-inserting keeps the grid a superset of every position an entity occupied
during this tick — including teleports (setorigin) and entities displaced by
earlier pushers. Stale entries are harmless: candidates are verified against
live state.
============
*/
void SV_PushGridEntityLinked (edict_t *ent)
{
	if (!push_grid_active || qcvm != push_grid_qcvm || ent->free)
		return;
	if (!SV_IsPushable (ent))
		return;
	PushGrid_Insert (ent);
}

/*
============
PushGrid_GatherCandidates

Collects pushable entities near the given box (in vanilla edict order, no
duplicates) into out. Returns the count, or -1 if the grid is unusable this
tick and the caller must scan the full cache.
============
*/
static int PushGrid_GatherCandidates (const vec3_t mins, const vec3_t maxs, edict_t **out)
{
	int num = 0;
	int query_cells = 1;

	if (!push_grid_valid)
		return -1;

	// strictly overlapping absboxes always share a cell, and riders touching the
	// pusher overlap it through the +-1 absbox expansion in SV_LinkEdict, so no
	// inflation is needed in theory. The +2 is a safety margin for riders whose
	// ONGROUND/groundentity state outlives actual contact by a small gap (the
	// elevator DIST_EPSILON nudge, float drift); it almost never adds a cell.
	int lo[3], hi[3];
	for (int axis = 0; axis < 3; ++axis)
		if (!isfinite (mins[axis]) || !isfinite (maxs[axis]) ||
			mins[axis] > maxs[axis])
			return -1;
	PushGrid_CellRange (mins, maxs, 2.0f, lo, hi);
	/* A very long pusher sweep can span far more cells than a canonical edict
	 * scan. Bound hash probes and use that scan rather than stalling here. */
	for (int axis = 0; axis < 3; ++axis)
	{
		const int span = hi[axis] - lo[axis] + 1;
		if (span <= 0 || span > PUSH_GRID_MAX_QUERY_CELLS / query_cells)
			return -1;
		query_cells *= span;
	}

	for (int x = lo[0]; x <= hi[0]; x++)
		for (int y = lo[1]; y <= hi[1]; y++)
			for (int z = lo[2]; z <= hi[2]; z++)
			{
				push_grid_cell_t key = {x, y, z};
				int32_t			*head = HashMap_Lookup (int32_t, push_grid_map, &key);
				for (int i = head ? *head : -1; i >= 0; i = push_grid_entries[i].next)
				{
					if (num == MAX_EDICTS)
						return -1; // pathological duplication, let the caller scan the cache
					out[num++] = push_grid_entries[i].ent;
				}
			}

	if (num + push_grid_num_large + (num_pushable_ent_cache - push_grid_tail_start) > MAX_EDICTS)
		return -1;

	for (int i = 0; i < push_grid_num_large; i++)
		out[num++] = push_grid_large[i];

	// entities allocated after the grid was built
	for (int i = push_grid_tail_start; i < num_pushable_ent_cache; i++)
		out[num++] = pushable_ent_cache[i];

	// Restore vanilla processing order (blocked pushers roll back everything
	// moved so far, so order is observable) and drop multi-cell duplicates.
	// Small lists are cheaper with insertion sort; larger ones use in-place
	// heapsort to avoid quadratic candidate ordering.
	if (num <= 16)
	{
		for (int i = 1; i < num; i++)
		{
			edict_t *key = out[i];
			int		 j = i - 1;
			while (j >= 0 && PushGrid_CompareEdictNumbers (out[j], key) > 0)
			{
				out[j + 1] = out[j];
				j--;
			}
			out[j + 1] = key;
		}
	}
	else
	{
		for (int root = num / 2 - 1; root >= 0; root--)
			PushGrid_SiftDown (out, root, num);
		for (int end = num - 1; end > 0; end--)
		{
			edict_t *key = out[end];
			out[end] = out[0];
			out[0] = key;
			PushGrid_SiftDown (out, 0, end);
		}
	}
	int unique = 0;
	for (int i = 0; i < num; i++)
		if (unique == 0 || out[unique - 1] != out[i])
			out[unique++] = out[i];

	return unique;
}

/*
================
SV_CheckAllEnts
================
*/
void SV_CheckAllEnts (void)
{
	int		 e;
	edict_t *check;

	// see if any solid entities are inside the final position
	check = NEXT_EDICT (qcvm->edicts);
	for (e = 1; e < qcvm->num_edicts; e++, check = NEXT_EDICT (check))
	{
		if (check->free)
			continue;
		if (check->v.movetype == MOVETYPE_PUSH || check->v.movetype == MOVETYPE_NONE || check->v.movetype == MOVETYPE_NOCLIP)
			continue;

		if (SV_TestEntityPosition (check))
			Con_Printf ("entity in invalid position\n");
	}
}

/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity (edict_t *ent)
{
	int i;

	//
	// bound velocity
	//
	for (i = 0; i < 3; i++)
	{
		if (IS_NAN (ent->v.velocity[i]))
		{
			Con_DPrintf ("Got a NaN velocity on %s\n", PR_GetString (ent->v.classname));
			ent->v.velocity[i] = 0;
		}
		if (IS_NAN (ent->v.origin[i]))
		{
			Con_DPrintf ("Got a NaN origin on %s\n", PR_GetString (ent->v.classname));
			ent->v.origin[i] = 0;
		}
		if (ent->v.velocity[i] > sv_maxvelocity.value)
			ent->v.velocity[i] = sv_maxvelocity.value;
		else if (ent->v.velocity[i] < -sv_maxvelocity.value)
			ent->v.velocity[i] = -sv_maxvelocity.value;
	}
}

/* QSS-M's customphysics callback replaces the native movement/Think
 * dispatcher. Keep the entity's body origin: this callback may move it,
 * so it must not enter the temporary VR weapon muzzle pose. */
static qboolean SV_RunCustomPhysics (edict_t *ent)
{
	eval_t *value = GetEdictFieldValue (ent, qcvm->extfields.customphysics);
	func_t function;
	qboolean friendly_fire_scope;

	if (!value || !value->function)
		return false;
	function = value->function;
	ED_Retain (ent);
	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
	PR_ExecuteProgram (function);
	if (friendly_fire_scope)
		SV_CoopFriendlyFireEnd ();
	ED_Release (ent);
	return true;
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
extern cvar_t sv_speeds;

double sv_speeds_think_ms, sv_speeds_pusher_ms, sv_speeds_build_ms;
int	   sv_speeds_thinks, sv_speeds_pushers, sv_speeds_pushables, sv_speeds_grid_entries;

static qboolean SV_RunThink (edict_t *ent)
{
	float	 thinktime;
	double	 think_start = 0;
	qboolean alive, friendly_fire_scope;

	thinktime = ent->v.nextthink;
	if (thinktime <= 0 || thinktime > qcvm->time + host_frametime)
		return true;

	if (sv_speeds.value && qcvm == &sv.qcvm)
		think_start = Sys_DoubleTime ();

	if (thinktime < qcvm->time)
		thinktime = qcvm->time; // don't let things stay in the past.
								// it is possible to start that way
								// by a trigger with a local time.

	ent->oldthinktime = thinktime;
	ent->oldframe = ent->v.frame; // johnfitz

	ent->v.nextthink = 0;
	pr_global_struct->time = thinktime;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	ED_Retain (ent);
	friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
	PR_ExecuteProgram (ent->v.think);
	if (friendly_fire_scope)
		SV_CoopFriendlyFireEnd ();

	ent->lastthink = 0;
	alive = !ent->free;
	if (alive && ent->v.groundentity && ent->v.nextthink > 0 && ent->v.nextthink - thinktime < 0.105f &&
		ent->v.groundentity <= (qcvm->num_edicts - 1) * qcvm->edict_size)
	{
		edict_t *pusher = PROG_TO_EDICT (ent->v.groundentity);
		if (!pusher->free)
		{
			float pusher_remaining = pusher->v.nextthink - pusher->v.ltime;
			if (pusher_remaining > 0)
			{
				float time = q_min ((int)((ent->v.nextthink - qcvm->time) / host_frametime) * host_frametime, pusher_remaining);
				for (int i = 0; i < 3; i++)
				{
					ent->predthinkpos[i] = ent->v.origin[i] + pusher->v.velocity[i] * time;
					if (pusher->v.velocity[i] != 0.0f)
						ent->lastthink = thinktime;
				}
			}
		}
	}
	ED_Release (ent);

	if (think_start != 0)
	{
		sv_speeds_think_ms += (Sys_DoubleTime () - think_start) * 1000.0;
		sv_speeds_thinks++;
	}

	return alive;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
static void SV_Impact (edict_t *e1, edict_t *e2)
{
	assert (!e1->free && !e2->free);

	int old_self, old_other, e1_prog, e2_prog;
	qboolean coop_touch_sync, friendly_fire_scope;

	old_self = pr_global_struct->self;
	old_other = pr_global_struct->other;
	e1_prog = EDICT_TO_PROG (e1);
	e2_prog = EDICT_TO_PROG (e2);

	pr_global_struct->time = qcvm->time;
	ED_Retain (e1);
	ED_Retain (e2);

	if (e1->v.touch && e1->v.solid != SOLID_NOT)
	{
		coop_touch_sync = SV_CoopSharedBeginClientTouch (e2);
		friendly_fire_scope = SV_CoopFriendlyFireBegin (e1);
		pr_global_struct->self = e1_prog;
		pr_global_struct->other = e2_prog;
		PR_ExecuteProgram (e1->v.touch);
		if (friendly_fire_scope)
			SV_CoopFriendlyFireEnd ();
		if (coop_touch_sync && !e2->free)
			SV_CoopSharedEndClientTouch (e2);
	}

	// Run e2's touch function if e2 survives e1's callback.
	if (!e2->free && e2->v.touch && e2->v.solid != SOLID_NOT)
	{
		coop_touch_sync = SV_CoopSharedBeginClientTouch (e1);
		friendly_fire_scope = SV_CoopFriendlyFireBegin (e2);
		pr_global_struct->self = e2_prog;
		pr_global_struct->other = e1_prog;
		PR_ExecuteProgram (e2->v.touch);
		if (friendly_fire_scope)
			SV_CoopFriendlyFireEnd ();
		if (coop_touch_sync && !e1->free)
			SV_CoopSharedEndClientTouch (e1);
	}

	ED_Release (e2);
	ED_Release (e1);

	pr_global_struct->self = old_self;
	pr_global_struct->other = old_other;
}

/*
==================
ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
#define STOP_EPSILON 0.1

static int ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	float backoff;
	float change;
	int	  i, blocked;

	blocked = 0;
	if (normal[2] > 0)
		blocked |= 1; // floor
	if (!normal[2])
		blocked |= 2; // step

	backoff = DotProduct (in, normal) * overbounce;

	for (i = 0; i < 3; i++)
	{
		change = normal[i] * backoff;
		out[i] = in[i] - change;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}

	return blocked;
}

typedef enum
{
	SV_PUSHER_CONTACT_NONE,
	SV_PUSHER_CONTACT_SUPPORT_FLOOR,
	SV_PUSHER_CONTACT_SUPPORT_SIDE
} sv_pusher_contact_t;

static sv_pusher_contact_t SV_ClassifyWalkSupportContact (trace_t *trace)
{
	float  support_dot;
	vec3_t tangent_normal;

	if (!sv_walk_support_pusher || trace->ent != sv_walk_support_pusher)
		return SV_PUSHER_CONTACT_NONE;

	support_dot = DotProduct (trace->plane.normal, sv_walk_support_normal);
	if (support_dot > MIN_WALK_NORMAL)
		return SV_PUSHER_CONTACT_SUPPORT_FLOOR;
	if (support_dot <= 0)
		return SV_PUSHER_CONTACT_NONE;

	// While walking on a pusher, a non-floor contact with that same pusher is
	// lateral support geometry.  Clip against its tangent component so it can't
	// inject velocity away from the support plane the client is standing on.
	VectorMA (trace->plane.normal, -support_dot, sv_walk_support_normal, tangent_normal);
	if (VectorNormalize (tangent_normal) <= DIST_EPSILON)
		return SV_PUSHER_CONTACT_NONE;

	VectorCopy (tangent_normal, trace->plane.normal);
	return SV_PUSHER_CONTACT_SUPPORT_SIDE;
}

/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
If move_velocity is supplied, use it for sweeps and clip both velocities.
The stored entity velocity must already include the full gravity update.
============
*/
#define MAX_CLIP_PLANES 5
static int SV_FlyMove (edict_t *ent, float time, const vec3_t move_velocity, trace_t *steptrace, qboolean callbacks)
{
	int					bumpcount, numbumps;
	vec3_t				dir;
	float				d;
	int					numplanes;
	vec3_t				planes[MAX_CLIP_PLANES];
	vec3_t				primal_velocity, original_velocity, new_velocity;
	vec3_t				sweep_velocity, original_end_velocity, new_end_velocity = {0}, impact_velocity;
	int					i, j;
	trace_t				trace;
	vec3_t				end;
	float				time_left;
	int					blocked;
	sv_pusher_contact_t pusher_contact;

	numbumps = 4;

	blocked = 0;
	VectorCopy (move_velocity ? move_velocity : ent->v.velocity, sweep_velocity);
	VectorCopy (sweep_velocity, original_velocity);
	VectorCopy (sweep_velocity, primal_velocity);
	VectorCopy (sweep_velocity, new_velocity);
	VectorCopy (ent->v.velocity, original_end_velocity);
	numplanes = 0;

	time_left = time;

	for (bumpcount = 0; bumpcount < numbumps; bumpcount++)
	{
		if (!sweep_velocity[0] && !sweep_velocity[1] && !sweep_velocity[2])
			break;

		for (i = 0; i < 3; i++)
			end[i] = ent->v.origin[i] + time_left * sweep_velocity[i];

		trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

		if (trace.allsolid)
		{ // entity is trapped in another solid
			VectorCopy (vec3_origin, ent->v.velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{ // actually covered some distance
			VectorCopy (trace.endpos, ent->v.origin);
			VectorCopy (sweep_velocity, original_velocity);
			VectorCopy (ent->v.velocity, original_end_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			break; // moved the entire distance

		if (!trace.ent)
			Sys_Error ("SV_FlyMove: !trace.ent");

		pusher_contact = SV_ClassifyWalkSupportContact (&trace);

		if (pusher_contact != SV_PUSHER_CONTACT_SUPPORT_SIDE && trace.plane.normal[2] > MIN_WALK_NORMAL)
		{
			blocked |= 1; // floor
			if (trace.ent->v.solid == SOLID_BSP)
			{
				ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
				ent->v.groundentity = EDICT_TO_PROG (trace.ent);
			}
		}
		if (pusher_contact == SV_PUSHER_CONTACT_SUPPORT_SIDE || !trace.plane.normal[2])
		{
			blocked |= 2; // step
			if (steptrace)
				*steptrace = trace; // save for player extrafriction
		}

		//
		// run the impact function
		//
		assert_always (!ent->free);

		VectorCopy (ent->v.velocity, impact_velocity);
		if (callbacks)
			SV_Impact (ent, trace.ent);
		if (ent->free)
			break; // removed by the impact function

		// The crease branch below uses the current velocity, including QC edits.
		// A replacement velocity must not inherit the old gravity sweep bias.
		if (!VectorCompare (ent->v.velocity, impact_velocity))
			VectorCopy (ent->v.velocity, sweep_velocity);

		time_left -= time_left * trace.fraction;

		// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{ // this shouldn't really happen
			VectorCopy (vec3_origin, ent->v.velocity);
			return 3;
		}

		VectorCopy (trace.plane.normal, planes[numplanes]);
		numplanes++;

		//
		// Clip both velocities against the same planes. A candidate must also
		// keep the end velocity out of every plane, even near a jump's apex.
		//
		for (i = 0; i < numplanes; i++)
		{
			ClipVelocity (original_velocity, planes[i], new_velocity, 1);
			ClipVelocity (original_end_velocity, planes[i], new_end_velocity, 1);
			for (j = 0; j < numplanes; j++)
				if (j != i)
				{
					if (DotProduct (new_velocity, planes[j]) < 0 || DotProduct (new_end_velocity, planes[j]) < 0)
						break; // not ok
				}
			if (j == numplanes)
				break;
		}

		if (i != numplanes)
		{ // go along this plane
			VectorCopy (new_velocity, sweep_velocity);
			VectorCopy (new_end_velocity, ent->v.velocity);
		}
		else
		{ // go along the crease
			if (numplanes != 2)
			{
				//				Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
				VectorCopy (vec3_origin, ent->v.velocity);
				return 7;
			}
			CrossProduct (planes[0], planes[1], dir);
			d = DotProduct (dir, sweep_velocity);
			VectorScale (dir, d, sweep_velocity);
			d = DotProduct (dir, ent->v.velocity);
			VectorScale (dir, d, ent->v.velocity);
		}

		//
		// if original velocity is against the original velocity, stop dead
		// to avoid tiny occilations in sloping corners
		//
		if (DotProduct (sweep_velocity, primal_velocity) <= 0)
		{
			VectorCopy (vec3_origin, ent->v.velocity);
			return blocked;
		}
	}

	return blocked;
}

static float SV_EntGravity (edict_t *ent)
{
	eval_t *val = GetEdictFieldValue (ent, ED_FindFieldOffset ("gravity"));
	return (val && val->_float) ? val->_float : 1.0f;
}

/*
============
SV_AddGravity

Apply gravity fully before movement and touch callbacks.
The separate sweep velocity preserves the canonical 72Hz freefall trajectory.
Clip both velocities on impact; no post-movement gravity update is needed.
============
*/
static void SV_AddGravity (edict_t *ent, vec3_t move_velocity)
{
	const float	 gravity = SV_EntGravity (ent) * sv_gravity.value;
	const double move_time = sv_analyticphysics_frame ? (host_frametime + 1.0 / MAX_PHYSICS_FREQ) * 0.5 : host_frametime;

	VectorCopy (ent->v.velocity, move_velocity);
	move_velocity[2] -= gravity * move_time;
	ent->v.velocity[2] -= gravity * host_frametime;
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

// 0=off; 1=legacy DIST_EPSILON nudge, clients only; 2=legacy nudge, all entities; 3=robust pusher contact (default)
cvar_t sv_gameplayfix_elevators = {"sv_gameplayfix_elevators", "3", CVAR_NONE};

// Private C-side movement frame for pusher support. QuakeC still observes the
// normal FL_ONGROUND, groundentity, origin and velocity contract.
typedef enum
{
	SV_MOVE_FRAME_NONE,
	SV_MOVE_FRAME_GROUND
} sv_client_move_frame_state_t;

typedef struct
{
	edict_t						*pusher;
	sv_client_move_frame_state_t state;
	vec3_t						 support_normal;
} sv_client_move_frame_t;

typedef struct
{
	unsigned					 frame;
	int							 pusher_entnum;
	sv_client_move_frame_state_t state;
	vec3_t						 pusher_move;
} sv_pusher_support_record_t;

typedef struct
{
	qboolean				   present;
	qboolean				   onground;
	int						   groundentity;
	sv_pusher_support_record_t record;
} sv_pusher_support_backup_t;

static void SV_BeginPusherSupportFrame (void)
{
	if (!qcvm->pusher_support)
		qcvm->pusher_support = HashMap_Create (int, sv_pusher_support_record_t, &HashInt32, NULL);

	qcvm->pusher_support_frame++;
	if (!qcvm->pusher_support_frame)
	{
		// counter wrapped, so every stored frame stamp is now meaningless
		HashMap_Destroy (qcvm->pusher_support);
		qcvm->pusher_support = HashMap_Create (int, sv_pusher_support_record_t, &HashInt32, NULL);
		qcvm->pusher_support_frame = 1;
	}
}

static qboolean SV_TracePusherFloorAtOrigin (edict_t *ent, edict_t *pusher, const vec3_t pusher_origin, float probe_distance, trace_t *trace)
{
	int	   i;
	vec3_t old_absmin, old_absmax;
	vec3_t old_origin, start, end;

	for (i = 0; i < 3; i++)
	{
		const float delta = pusher_origin[i] - pusher->v.origin[i];
		old_absmin[i] = pusher->v.absmin[i] + delta;
		old_absmax[i] = pusher->v.absmax[i] + delta;
	}

	if (ent->v.absmin[0] >= old_absmax[0] || ent->v.absmin[1] >= old_absmax[1] || ent->v.absmax[0] <= old_absmin[0] || ent->v.absmax[1] <= old_absmin[1])
		return false;
	if (ent->v.absmin[2] > old_absmax[2] + probe_distance || ent->v.absmax[2] < old_absmin[2] - PUSH_CONTACT_EPSILON)
		return false;

	VectorCopy (pusher->v.origin, old_origin);
	VectorCopy (pusher_origin, pusher->v.origin);

	VectorCopy (ent->v.origin, start);
	start[2] += PUSH_CONTACT_EPSILON;
	VectorCopy (ent->v.origin, end);
	end[2] -= probe_distance;
	*trace = SV_ClipMoveToEntity (pusher, start, ent->v.mins, ent->v.maxs, end, CONTENTMASK_ANYSOLID);

	VectorCopy (old_origin, pusher->v.origin);

	return !trace->startsolid && trace->fraction < 1 && trace->plane.normal[2] > MIN_WALK_NORMAL;
}

static float SV_PusherMoveTimeThisFrame (edict_t *pusher)
{
	float thinktime;
	float movetime;

	thinktime = pusher->v.nextthink;
	if (thinktime < pusher->v.ltime + host_frametime)
	{
		movetime = thinktime - pusher->v.ltime;
		if (movetime < 0)
			movetime = 0;
	}
	else
		movetime = host_frametime;

	return movetime;
}

static qboolean SV_IsSupportPusher (edict_t *pusher)
{
	if (!pusher || pusher->free)
		return false;
	if (pusher->v.movetype != MOVETYPE_PUSH || pusher->v.solid != SOLID_BSP)
		return false;
	return true;
}

static qboolean SV_PusherWillMoveThisFrame (edict_t *pusher)
{
	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2])
		return false;
	return SV_PusherMoveTimeThisFrame (pusher) > 0;
}

static edict_t *SV_GetGroundPusher (edict_t *ent)
{
	edict_t *ground;

	if (sv_gameplayfix_elevators.value < 3.f || !((int)ent->v.flags & FL_ONGROUND))
		return NULL;
	if (ent->v.groundentity <= 0 || ent->v.groundentity > (qcvm->num_edicts - 1) * qcvm->edict_size)
		return NULL;

	ground = PROG_TO_EDICT (ent->v.groundentity);
	if (!SV_IsSupportPusher (ground))
		return NULL;

	return ground;
}

// Returns NULL when the entity has no record; only riders have one.
static sv_pusher_support_record_t *SV_GetPusherSupportRecord (edict_t *ent)
{
	int entnum;

	if (!qcvm->pusher_support)
		return NULL;

	entnum = NUM_FOR_EDICT (ent);
	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return NULL;

	return HashMap_Lookup (sv_pusher_support_record_t, qcvm->pusher_support, &entnum);
}

static void SV_GetAppliedPusherSupportMove (edict_t *ent, vec3_t move)
{
	const sv_pusher_support_record_t *support = SV_GetPusherSupportRecord (ent);

	VectorCopy (vec3_origin, move);
	if (!support)
		return;
	if (support->frame == qcvm->pusher_support_frame)
		VectorCopy (support->pusher_move, move);
}

// A missing record is a valid state to restore, so the backup carries a flag
// rather than relying on a zeroed record meaning "absent". Public ground state
// is part of the same transaction because recording support updates it too.
static void SV_BackupPusherSupport (edict_t *ent, sv_pusher_support_backup_t *backup)
{
	const sv_pusher_support_record_t *support = SV_GetPusherSupportRecord (ent);

	memset (backup, 0, sizeof (*backup));
	backup->onground = (int)ent->v.flags & FL_ONGROUND;
	backup->groundentity = ent->v.groundentity;
	if (support)
	{
		backup->present = true;
		backup->record = *support;
	}
}

static void SV_RestorePusherSupport (edict_t *ent, const sv_pusher_support_backup_t *backup)
{
	int entnum = NUM_FOR_EDICT (ent);

	if (backup->onground)
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
	else
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
	ent->v.groundentity = backup->groundentity;

	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return;

	if (backup->present)
		HashMap_Insert (qcvm->pusher_support, &entnum, &backup->record);
	else
		HashMap_Erase (qcvm->pusher_support, &entnum);
}

static qboolean SV_WritePusherSupportRecord (edict_t *ent, edict_t *pusher, sv_client_move_frame_state_t state, const vec3_t pusher_move)
{
	int						   pushernum, entnum;
	sv_pusher_support_record_t record;

	pushernum = NUM_FOR_EDICT (pusher);
	if (pushernum <= 0 || pushernum >= MAX_EDICTS)
		return false;
	entnum = NUM_FOR_EDICT (ent);
	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return false;

	record.frame = qcvm->pusher_support_frame;
	record.pusher_entnum = pushernum;
	record.state = state;
	VectorCopy (pusher_move, record.pusher_move);

	HashMap_Insert (qcvm->pusher_support, &entnum, &record);
	return true;
}

static qboolean SV_EntityGroundEntityIsPusher (edict_t *ent, edict_t *pusher)
{
	if (ent->v.groundentity <= 0 || ent->v.groundentity > (qcvm->num_edicts - 1) * qcvm->edict_size)
		return false;

	return PROG_TO_EDICT (ent->v.groundentity) == pusher;
}

static qboolean SV_MovetypeUsesGroundFlag (edict_t *ent)
{
	return ent->v.movetype == MOVETYPE_WALK || ent->v.movetype == MOVETYPE_STEP || ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_GIB;
}

static qboolean SV_EntityClaimsPusherSupport (edict_t *ent, edict_t *pusher)
{
	if (SV_MovetypeUsesGroundFlag (ent))
		return ((int)ent->v.flags & FL_ONGROUND) && SV_EntityGroundEntityIsPusher (ent, pusher);

	// Other movetypes do not consistently use FL_ONGROUND/groundentity, but an
	// explicit assignment to another ground entity still relinquishes support.
	return !ent->v.groundentity || SV_EntityGroundEntityIsPusher (ent, pusher);
}

static qboolean SV_HasRecentPusherSupportRecord (edict_t *ent, edict_t *pusher)
{
	const sv_pusher_support_record_t *support = SV_GetPusherSupportRecord (ent);

	if (!support)
		return false;
	if (support->state != SV_MOVE_FRAME_GROUND)
		return false;
	if (support->pusher_entnum != NUM_FOR_EDICT (pusher))
		return false;

	// carried this frame or the one before it; older records are stale
	return support->frame == qcvm->pusher_support_frame || support->frame + 1 == qcvm->pusher_support_frame;
}

// groundentity only identifies the pusher to probe; support still requires a floor trace.
static qboolean SV_EntityHasPusherSupportAtOrigin (edict_t *ent, edict_t *pusher, const vec3_t pusher_origin, trace_t *trace)
{
	if (!SV_IsSupportPusher (pusher))
		return false;

	// QuakeC can explicitly leave established support (a player jump, fiend
	// pounce, etc.) after this entity's physics turn but before the pusher runs.
	// The old geometric contact lasts for the remainder of that tick and must
	// not re-establish the support record. Initial contact remains geometry-based
	// because some entity types do not maintain public ground state consistently.
	if (SV_HasRecentPusherSupportRecord (ent, pusher) && !SV_EntityClaimsPusherSupport (ent, pusher))
		return false;

	if (ent->v.movetype == MOVETYPE_WALK && !((int)ent->v.flags & FL_ONGROUND) && !SV_EntityGroundEntityIsPusher (ent, pusher))
		return false;

	return SV_TracePusherFloorAtOrigin (ent, pusher, pusher_origin, PUSH_CONTACT_EPSILON, trace);
}

// Contact established by a floor trace persists until something positively
// breaks it. Re-deriving support from geometry every frame lets float rounding
// drop a rider that never actually left the pusher.
static qboolean SV_HasPersistentPusherSupport (edict_t *ent, edict_t *pusher)
{
	if (!SV_HasRecentPusherSupportRecord (ent, pusher))
		return false;
	// QuakeC can jump, teleport, or reassign ground state after this entity's
	// release pass but before a later pusher consumes the record.
	if (!SV_EntityClaimsPusherSupport (ent, pusher))
		return false;

	return true;
}

static void SV_BreakPusherSupport (edict_t *ent)
{
	int entnum = NUM_FOR_EDICT (ent);

	if (entnum <= 0 || entnum >= MAX_EDICTS)
		return;

	HashMap_Erase (qcvm->pusher_support, &entnum);
}

static void SV_ClearRecordedPusherSupport (edict_t *ent, edict_t *pusher)
{
	SV_BreakPusherSupport (ent);

	// Only clear public ground state backed by our private support record.
	// Mods may use FL_ONGROUND/groundentity for their own movement logic even
	// when the named pusher is not geometrically beneath the entity.
	if (SV_EntityGroundEntityIsPusher (ent, pusher))
	{
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
		ent->v.groundentity = 0;
	}
}

static void SV_RecordPusherSupport (edict_t *ent, edict_t *pusher, const vec3_t pusher_move)
{
	trace_t trace;

	if (sv_gameplayfix_elevators.value < 3.f)
		return;

	// an established rider wedged against a neighbour fails the floor trace even
	// though it never left the pusher, so keep carrying it on the stored record
	if (!SV_EntityHasPusherSupportAtOrigin (ent, pusher, pusher->v.origin, &trace) && !SV_HasPersistentPusherSupport (ent, pusher))
		return;
	if (!SV_WritePusherSupportRecord (ent, pusher, SV_MOVE_FRAME_GROUND, pusher_move))
		return;

	if (SV_MovetypeUsesGroundFlag (ent))
	{
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG (pusher);
	}
}

static void SV_ClearClientMoveFrame (sv_client_move_frame_t *frame)
{
	frame->pusher = NULL;
	frame->state = SV_MOVE_FRAME_NONE;
	VectorCopy (vec3_origin, frame->support_normal);
}

static void SV_SetClientPusherMoveFrame (sv_client_move_frame_t *frame, edict_t *pusher, sv_client_move_frame_state_t state, const float *support_normal)
{
	frame->pusher = pusher;
	frame->state = state;
	if (support_normal)
		VectorCopy (support_normal, frame->support_normal);
	else
		VectorCopy (vec3_origin, frame->support_normal);
}

static qboolean SV_CaptureRecordedPusherMoveFrame (edict_t *ent, sv_client_move_frame_t *frame, const sv_pusher_support_record_t *record, edict_t *pusher)
{
	trace_t trace;

	switch (record->state)
	{
	case SV_MOVE_FRAME_GROUND:
		if (ent->v.groundentity != EDICT_TO_PROG (pusher))
			return false;
		if (!SV_EntityHasPusherSupportAtOrigin (ent, pusher, pusher->v.origin, &trace))
			return false;

		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		SV_SetClientPusherMoveFrame (frame, pusher, SV_MOVE_FRAME_GROUND, trace.plane.normal);
		return true;

	default:
		return false;
	}
}

static void SV_CaptureClientMoveFrameBeforeQC (edict_t *ent, sv_client_move_frame_t *frame)
{
	const sv_pusher_support_record_t *record;
	edict_t							 *pusher;
	trace_t							  trace;

	SV_ClearClientMoveFrame (frame);
	if (sv_gameplayfix_elevators.value < 3.f)
		return;

	record = SV_GetPusherSupportRecord (ent);
	if (record && record->frame && record->frame + 1 == qcvm->pusher_support_frame && record->pusher_entnum > 0 && record->pusher_entnum < qcvm->num_edicts)
	{
		pusher = EDICT_NUM (record->pusher_entnum);
		if (SV_IsSupportPusher (pusher))
		{
			if (SV_CaptureRecordedPusherMoveFrame (ent, frame, record, pusher))
				return;
		}
	}

	pusher = SV_GetGroundPusher (ent);
	if (pusher && SV_PusherWillMoveThisFrame (pusher) && SV_EntityHasPusherSupportAtOrigin (ent, pusher, pusher->v.origin, &trace))
		SV_SetClientPusherMoveFrame (frame, pusher, SV_MOVE_FRAME_GROUND, trace.plane.normal);
}

static qboolean SV_ClientMoveFrameHasGroundSupport (const sv_client_move_frame_t *frame)
{
	if (frame->state != SV_MOVE_FRAME_GROUND)
		return false;
	if (!SV_IsSupportPusher (frame->pusher))
		return false;
	if (DotProduct (frame->support_normal, frame->support_normal) <= DIST_EPSILON * DIST_EPSILON)
		return false;
	return true;
}

static void SV_SetWalkMoveFrameClipContext (const sv_client_move_frame_t *move_frame)
{
	if (!SV_ClientMoveFrameHasGroundSupport (move_frame))
	{
		sv_walk_support_pusher = NULL;
		VectorCopy (vec3_origin, sv_walk_support_normal);
		return;
	}

	sv_walk_support_pusher = move_frame->pusher;
	VectorCopy (move_frame->support_normal, sv_walk_support_normal);
}

static void SV_ClearWalkSupportClipContext (void)
{
	sv_walk_support_pusher = NULL;
	VectorCopy (vec3_origin, sv_walk_support_normal);
}

static int
SV_FlyMoveWithMoveFrameClipContext (edict_t *ent, float time, const sv_client_move_frame_t *move_frame, const vec3_t move_velocity, trace_t *steptrace, qboolean callbacks)
{
	int clip;

	SV_SetWalkMoveFrameClipContext (move_frame);
	clip = SV_FlyMove (ent, time, move_velocity, steptrace, callbacks);
	SV_ClearWalkSupportClipContext ();
	return clip;
}

static void SV_DropClientMoveFramePusherGround (edict_t *ent, sv_client_move_frame_t *move_frame)
{
	edict_t *pusher = move_frame->pusher;

	SV_ClearClientMoveFrame (move_frame);
	if (ent->v.groundentity == EDICT_TO_PROG (pusher))
		ent->v.groundentity = 0;
}

static void SV_UpdateClientMoveFrameAfterQC (edict_t *ent, sv_client_move_frame_t *move_frame)
{
	if (!move_frame->pusher)
		return;

	if (move_frame->state != SV_MOVE_FRAME_GROUND)
		return;

	if (!((int)ent->v.flags & FL_ONGROUND) && ent->v.movetype == MOVETYPE_WALK)
		SV_DropClientMoveFramePusherGround (ent, move_frame);
}

static qboolean SV_GroundClientOnMoveFramePusher (edict_t *ent, const sv_client_move_frame_t *move_frame)
{
	trace_t trace;

	if (!SV_ClientMoveFrameHasGroundSupport (move_frame))
		return false;
	if (!SV_TracePusherFloorAtOrigin (ent, move_frame->pusher, move_frame->pusher->v.origin, STEPSIZE, &trace))
		return false;

	VectorCopy (trace.endpos, ent->v.origin);
	SV_LinkEdict (ent, false);
	ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
	ent->v.groundentity = EDICT_TO_PROG (move_frame->pusher);
	ent->v.velocity[2] = 0;
	return true;
}

static qboolean SV_TestEntityPositionOnPusher (edict_t *ent, edict_t *pusher, const vec3_t pusher_origin, const vec3_t ent_origin)
{
	vec3_t	old_origin;
	vec3_t	trace_origin;
	trace_t trace;

	VectorCopy (pusher->v.origin, old_origin);
	VectorCopy (pusher_origin, pusher->v.origin);
	VectorCopy (ent_origin, trace_origin);
	trace = SV_ClipMoveToEntity (pusher, trace_origin, ent->v.mins, ent->v.maxs, trace_origin, CONTENTMASK_ANYSOLID);
	VectorCopy (old_origin, pusher->v.origin);
	return trace.startsolid;
}

static qboolean SV_EntityPositionBlockedIgnoringPusher (edict_t *ent, edict_t *pusher)
{
	float	 solid_backup;
	qboolean blocked;

	solid_backup = pusher->v.solid;
	pusher->v.solid = SOLID_NOT;
	blocked = SV_TestEntityPosition (ent) != NULL;
	pusher->v.solid = solid_backup;
	return blocked;
}

static qboolean SV_EntityRidingPusher (edict_t *ent, edict_t *pusher)
{
	return ((int)ent->v.flags & FL_ONGROUND) && SV_EntityGroundEntityIsPusher (ent, pusher);
}

// Owns the release decision for persistent support at the entity's physics
// turn. Consumers re-check the live ground state because QuakeC can change it
// again later in the same frame.
static void SV_UpdatePersistentPusherSupport (edict_t *ent)
{
	const sv_pusher_support_record_t *support;
	edict_t							 *pusher;
	trace_t							  trace;
	int								  pusher_entnum;

	if (sv_gameplayfix_elevators.value < 3.f)
		return;

	support = SV_GetPusherSupportRecord (ent);
	if (!support)
		return;
	if (support->state != SV_MOVE_FRAME_GROUND || support->pusher_entnum <= 0)
		return;

	// the record lives in hash map storage that any erase can move, so read what
	// is needed before touching the map again
	pusher_entnum = support->pusher_entnum;
	support = NULL;

	if (pusher_entnum >= qcvm->num_edicts)
	{
		SV_BreakPusherSupport (ent);
		return;
	}

	pusher = EDICT_NUM (pusher_entnum);
	if (!SV_IsSupportPusher (pusher))
	{
		if (pusher->free || pusher->v.solid == SOLID_NOT || pusher->v.solid == SOLID_TRIGGER)
			SV_ClearRecordedPusherSupport (ent, pusher);
		else
			SV_BreakPusherSupport (ent);
		return;
	}

	// left the ground under its own power, or QuakeC moved it onto something else
	if (!SV_EntityClaimsPusherSupport (ent, pusher))
	{
		SV_BreakPusherSupport (ent);
		return;
	}

	if (!SV_TracePusherFloorAtOrigin (ent, pusher, pusher->v.origin, PUSH_RELEASE_EPSILON, &trace))
		SV_ClearRecordedPusherSupport (ent, pusher);
}

// Adopt an existing QuakeC ground claim only after geometry confirms it. This
// gives riders present at spawn/load the same persistent support as riders
// already carried by SV_PushMove, without rewriting non-geometric ground state
// used by mods for custom movement.
static void SV_AdoptPusherSupport (edict_t *ent)
{
	edict_t *pusher;
	trace_t	 trace;

	if (sv_gameplayfix_elevators.value < 3.f || SV_GetPusherSupportRecord (ent))
		return;

	pusher = SV_GetGroundPusher (ent);
	if (!pusher)
		return;
	if (!SV_EntityHasPusherSupportAtOrigin (ent, pusher, pusher->v.origin, &trace))
		return;

	SV_WritePusherSupportRecord (ent, pusher, SV_MOVE_FRAME_GROUND, vec3_origin);
}

static qboolean SV_PusherBoundsOverlapEntity (edict_t *ent, const vec3_t mins, const vec3_t maxs)
{
	return !(
		ent->v.absmin[0] >= maxs[0] || ent->v.absmin[1] >= maxs[1] || ent->v.absmin[2] >= maxs[2] || ent->v.absmax[0] <= mins[0] ||
		ent->v.absmax[1] <= mins[1] || ent->v.absmax[2] <= mins[2]);
}

static qboolean
SV_PusherAffectsEntity (edict_t *ent, edict_t *pusher, const vec3_t pushorig, const vec3_t mins, const vec3_t maxs, qboolean robust_push, qboolean *riding)
{
	trace_t support_trace;

	*riding = false;

	if (robust_push && (SV_EntityHasPusherSupportAtOrigin (ent, pusher, pushorig, &support_trace) || SV_HasPersistentPusherSupport (ent, pusher)))
	{
		*riding = true;
		return true;
	}

	if (!robust_push && SV_EntityRidingPusher (ent, pusher))
	{
		*riding = true;
		return true;
	}

	if (!SV_PusherBoundsOverlapEntity (ent, mins, maxs))
		return false;

	if (!robust_push)
	{
		if (pusher->v.skin < 0)
			return SV_ClipMoveToEntity (pusher, ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, CONTENTMASK_ANYSOLID).startsolid;
		return SV_TestEntityPosition (ent) != NULL;
	}

	// Test the active pusher only; SV_TestEntityPosition can report an
	// unrelated platform the entity is already standing on.
	return SV_TestEntityPositionOnPusher (ent, pusher, pusher->v.origin, ent->v.origin);
}

static qboolean SV_PusherBlockIsPersistentRiderContact (
	edict_t *ent, edict_t *pusher, edict_t *block, const vec3_t pushorig, const vec3_t entorig, qboolean robust_push, qboolean riding)
{
	if (!robust_push || !riding || block != pusher)
		return false;

	// Existing rider contact with this pusher is not a new crush.
	return SV_TestEntityPositionOnPusher (ent, pusher, pushorig, entorig);
}

static trace_t SV_PushEntityMove (edict_t *ent, vec3_t start, vec3_t end)
{
	if (ent->v.movetype == MOVETYPE_FLYMISSILE)
		return SV_Move (start, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent);
	else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
		// only clip against bmodels
		return SV_Move (start, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent);
	else
		return SV_Move (start, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);
}

static trace_t SV_PushEntityMoveWithIgnoreMask (edict_t *ent, vec3_t start, vec3_t end, const sv_ignore_edicts_t *ignore_mask)
{
	if (!ignore_mask)
		return SV_PushEntityMove (ent, start, end);
	if (ent->v.movetype == MOVETYPE_FLYMISSILE)
		return SV_MoveWithEdictIgnoreMask (start, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent, ignore_mask);
	else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
		return SV_MoveWithEdictIgnoreMask (start, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent, ignore_mask);
	else
		return SV_MoveWithEdictIgnoreMask (start, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent, ignore_mask);
}

static edict_t *SV_TestEntityPositionWithIgnoreMask (edict_t *ent, const sv_ignore_edicts_t *ignore_mask)
{
	trace_t trace;

	if (!ignore_mask)
		return SV_TestEntityPosition (ent);

	trace = SV_MoveWithEdictIgnoreMask (ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, 0, ent, ignore_mask);
	if (trace.startsolid)
		return trace.ent ? trace.ent : qcvm->edicts;

	return NULL;
}

/*
============
SV_PushEntityTo

Does not change the entities velocity at all
============
*/
static trace_t SV_PushEntityToWithIgnoreMask (edict_t *ent, vec3_t end, const sv_ignore_edicts_t *ignore_mask, qboolean callbacks)
{
	trace_t trace;

	trace = SV_PushEntityMoveWithIgnoreMask (ent, ent->v.origin, end, ignore_mask);

	// a move that starts solid registers no impact, so an entity marginally inside the
	// pusher it rests on would glide through it and fall out the far side. un-embed
	// with a sweep against the pusher and redo the move so it collides normally.
	if (trace.startsolid && ent->v.groundentity && sv_gameplayfix_elevators.value >= 3.f)
	{
		edict_t *ground = PROG_TO_EDICT (ent->v.groundentity);
		if (ground != qcvm->edicts && !ground->free && ground->v.movetype == MOVETYPE_PUSH && ground->v.solid == SOLID_BSP &&
			SV_ClipMoveToEntity (ground, ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, CONTENTMASK_ANYSOLID).startsolid)
		{
			vec3_t	above;
			trace_t exit;

			VectorCopy (ent->v.origin, above);
			above[2] += PUSH_CONTACT_EPSILON;
			exit = SV_ClipMoveToEntity (ground, above, ent->v.mins, ent->v.maxs, ent->v.origin, CONTENTMASK_ANYSOLID);
			if (!exit.startsolid && exit.fraction < 1)
			{
				Con_DPrintf2 ("SV_PushEntityTo: un-embedded entity %i from pusher %i\n", NUM_FOR_EDICT (ent), NUM_FOR_EDICT (ground));
				VectorCopy (exit.endpos, ent->v.origin);
				trace = SV_PushEntityMoveWithIgnoreMask (ent, ent->v.origin, end, ignore_mask);
			}
		}
	}

	if (trace.ent)
		assert_always (!trace.ent->free);

	VectorCopy (trace.endpos, ent->v.origin);

	ED_Retain (ent);
	if (trace.ent)
		ED_Retain (trace.ent);

	SV_LinkEdict (ent, callbacks);

	// Run the impact only while both collision participants still exist.
	if (callbacks && !ent->free && trace.ent && !trace.ent->free)
		SV_Impact (ent, trace.ent);

	if (trace.ent)
		ED_Release (trace.ent);
	ED_Release (ent);

	return trace;
}

static trace_t SV_PushEntityTo (edict_t *ent, vec3_t end, qboolean callbacks)
{
	return SV_PushEntityToWithIgnoreMask (ent, end, NULL, callbacks);
}

// Appends in the caller's order, which for pusher candidates is already sorted.
static void SV_IgnoreEdictsAddRider (sv_ignore_edicts_t *list, edict_t *ent)
{
	// lookups binary search this, so an unsorted append would silently miss
	assert (!list->num_riders || list->riders[list->num_riders - 1] < ent);
	list->riders[list->num_riders++] = ent;
}

/*
============
SV_PushMove
============
*/

static void SV_PushMove (edict_t *pusher, float movetime)
{
	int		 i;
	edict_t *check, *block;
	vec3_t	 mins, maxs, move;
	vec3_t	 entorig, pushorig;
	vec3_t	 querymins, querymaxs;
	int		 num_moved;

	if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2])
	{
		pusher->v.ltime += movetime;
		return;
	}

	const qboolean robust_push = (sv_gameplayfix_elevators.value >= 3.f);
	const float	   newltime = pusher->v.ltime + movetime;
	vec3_t		   neworigin;

	// PushGrid_GatherCandidates fills this up to MAX_EDICTS before it gives up
	TEMP_ALLOC_COND (edict_t *, push_candidates, MAX_EDICTS, push_grid_active);

	// everything below holds at most one entry per candidate, so it is sized once
	// the candidate list is known
	TEMP_ALLOC_DECL (edict_t *, moved_edict);
	TEMP_ALLOC_DECL (vec3_t, moved_from);
	TEMP_ALLOC_DECL (sv_pusher_support_backup_t, moved_support);
	TEMP_ALLOC_DECL (edict_t *, push_edict);
	TEMP_ALLOC_DECL (qboolean, push_riding);
	TEMP_ALLOC_DECL (edict_t *, rider_ignore_storage);

	sv_ignore_edicts_t pusher_ignore_mask = {NULL, 0, pusher};
	sv_ignore_edicts_t rider_ignore_mask = {NULL, 0, NULL};
	sv_ignore_edicts_t pusher_rider_ignore_mask = {NULL, 0, pusher};

	VectorScale (pusher->v.velocity, movetime, move);
	VectorAdd (pusher->v.origin, move, neworigin);
	for (i = 0; i < 3; i++)
	{
		mins[i] = pusher->v.absmin[i] + move[i];
		maxs[i] = pusher->v.absmax[i] + move[i];
		// the grid query must span the whole sweep: riders rest on the pre-move
		// box and are exempt from the final-box overlap test below
		querymins[i] = q_min (pusher->v.absmin[i], mins[i]);
		querymaxs[i] = q_max (pusher->v.absmax[i], maxs[i]);
	}

	VectorCopy (pusher->v.origin, pushorig);
	ED_Retain (pusher);

	// move the pusher to it's final position

	VectorCopy (neworigin, pusher->v.origin);
	pusher->v.ltime = newltime;
	SV_LinkEdict (pusher, false);

	// see if any solid entities are inside the final position
	num_moved = 0;

	edict_t **fast_list = NULL;
	int		  fast_count = 0;

	if (push_grid_active)
	{
		fast_count = PushGrid_GatherCandidates (querymins, querymaxs, push_candidates);
		if (fast_count >= 0)
			fast_list = push_candidates;
		// If the grid is unusable this tick, leave fast_list NULL and fall
		// back to the canonical edict scan below.
	}

	const int max_candidates = fast_list ? fast_count : qcvm->num_edicts;
	TEMP_ALLOC_ASSIGN_COND (rider_ignore_storage, max_candidates, robust_push);
	if (robust_push)
	{
		// both views share the rider storage and differ only in whether the pusher
		// is ignored too
		rider_ignore_mask.riders = rider_ignore_storage;
		pusher_rider_ignore_mask.riders = rider_ignore_storage;
	}
	TEMP_ALLOC_ASSIGN (moved_edict, max_candidates);
	TEMP_ALLOC_ASSIGN (moved_from, max_candidates);
	TEMP_ALLOC_ASSIGN (moved_support, max_candidates);
	TEMP_ALLOC_ASSIGN_COND (push_edict, max_candidates, robust_push);
	TEMP_ALLOC_ASSIGN_COND (push_riding, max_candidates, robust_push);

	int num_push = 0;

	if (robust_push)
	{
		int		 scan_e = -1;
		edict_t *scan_check = NEXT_EDICT (qcvm->edicts);

		while (true)
		{
			qboolean riding;

			// bounded by max_candidates, which is what push_edict/push_riding were
			// sized to; the trailing -1 skips entity 0
			if (scan_e >= (fast_list ? fast_count - 1 : max_candidates - 1 - 1))
				break;

			scan_e++;

			if (fast_list)
			{
				scan_check = fast_list[scan_e];
			}
			else if (scan_e > 0)
			{
				scan_check = NEXT_EDICT (scan_check);
			}

			if (scan_check->free)
				continue;

			if (!SV_IsPushable (scan_check))
				continue;

			if (!SV_PusherAffectsEntity (scan_check, pusher, pushorig, mins, maxs, true, &riding))
				continue;

			push_edict[num_push] = scan_check;
			push_riding[num_push] = riding;
			ED_Retain (scan_check);
			num_push++;

			if (riding)
				SV_IgnoreEdictsAddRider (&rider_ignore_mask, scan_check);
		}

		// candidates arrive in ascending pointer order, so the list is already
		// sorted; both masks view the same storage
		pusher_rider_ignore_mask.num_riders = rider_ignore_mask.num_riders;
	}

	int e = -1;

	// beware, we skip entity 0:
	check = NEXT_EDICT (qcvm->edicts);

	while (true)
	{
		// max_candidates rather than a fresh num_edicts read: the pusher's blocked
		// function can spawn entities, and the scratch arrays were already sized
		if (e >= (robust_push ? num_push - 1 : (fast_list ? fast_count - 1 : max_candidates - 1 - 1)))
			break;

		e++;

		qboolean riding = false;

		if (robust_push)
		{
			check = push_edict[e];
			riding = push_riding[e];
		}
		else
		{
			if (fast_list)
			{
				check = fast_list[e];
			}
			else if (e > 0)
			{
				check = NEXT_EDICT (check);
			}

			if (check->free)
				continue;

			if (!SV_IsPushable (check))
				continue;

			if (!SV_PusherAffectsEntity (check, pusher, pushorig, mins, maxs, false, &riding))
				continue;
		}

		if (check->free)
			continue;

		if (!SV_IsPushable (check))
			continue;

		/* Client movement ran earlier in this world frame. Native pusher
		 * carry/contact remains authoritative, so withhold private replay. */
		if (qcvm == &sv.qcvm)
		{
			int slot = NUM_FOR_EDICT (check);
			if (slot > 0 && slot <= svs.maxclients &&
				svs.clients[slot - 1].active &&
				svs.clients[slot - 1].edict == check &&
				svs.clients[slot - 1].private_pmove_walk_selected)
				svs.clients[slot - 1].private_pmove_pusher_interaction = true;
		}

		// remove the onground flag for non-players. riders keep it under robust
		// push: the support layer owns their ground state, and a transient clear
		// that survives a blocked rollback reads as the rider leaving the ground,
		// which erases its support record
		if (check->v.movetype != MOVETYPE_WALK && !(robust_push && riding))
			check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

		VectorCopy (check->v.origin, entorig);
		VectorCopy (check->v.origin, moved_from[num_moved]);
		SV_BackupPusherSupport (check, &moved_support[num_moved]);
		moved_edict[num_moved] = check;
		ED_Retain (check);
		num_moved++;

		// QIP fix for end.bsp
		if (pusher->v.solid == SOLID_BSP		  // everything that blocks: bsp models = map brushes = doors, plats, etc.
			|| pusher->v.solid == SOLID_BBOX	  // normally boxes
			|| pusher->v.solid == SOLID_SLIDEBOX) // normally monsters
		{
			const sv_ignore_edicts_t *move_ignore_mask = (robust_push && riding) ? &pusher_rider_ignore_mask : &pusher_ignore_mask;
			const sv_ignore_edicts_t *block_ignore_mask = (robust_push && riding) ? &rider_ignore_mask : NULL;
			vec3_t					  dest;

			if (robust_push)
			{
				vec3_t applied_move;

				if (riding)
					SV_GetAppliedPusherSupportMove (check, applied_move);
				else
					VectorCopy (vec3_origin, applied_move);

				// Supported entities are carried by the pusher frame once per
				// physics frame. Composite movers therefore apply only the
				// difference from the support motion already applied.
				for (i = 0; i < 3; i++)
					dest[i] = entorig[i] + move[i] - applied_move[i];
			}
			else
				VectorAdd (entorig, move, dest);

			// try moving the contacted entity
			SV_PushEntityToWithIgnoreMask (check, dest, move_ignore_mask, true);
			if (pusher->free)
				break;
			if (check->free)
				continue;

			// if it is still inside the pusher, block
			if (pusher->v.skin < 0)
			{ // if it has forced contents then do things in a slightly different order, so water can push properly.
				block = SV_TestEntityPositionWithIgnoreMask (check, move_ignore_mask);
			}
			else
				block = SV_TestEntityPositionWithIgnoreMask (check, block_ignore_mask);
		}
		else
			block = NULL;
		if (block)
		{ // fail the move
			if (check->v.mins[0] == check->v.maxs[0])
				continue;

			if (SV_PusherBlockIsPersistentRiderContact (check, pusher, block, pushorig, entorig, robust_push, riding))
			{
				if (riding)
					SV_RecordPusherSupport (check, pusher, move);
				continue;
			}

			// riders only embed through their ground contact and never deeper than
			// PUSH_CONTACT_EPSILON, so a single sweep from above recovers the exact
			// contact position. must run before the corpse path so items don't get
			// their bbox zeroed over a rounding error; real squeezes still crush.
			if (robust_push && riding && block == pusher)
			{
				vec3_t	pushedorg, above;
				trace_t settle;

				VectorCopy (check->v.origin, pushedorg);
				VectorCopy (check->v.origin, above);
				above[2] += PUSH_CONTACT_EPSILON;
				settle = SV_PushEntityMove (check, above, pushedorg);
				if (!settle.startsolid)
				{
					VectorCopy (settle.endpos, check->v.origin);
					if (!SV_TestEntityPositionWithIgnoreMask (check, &rider_ignore_mask))
					{
						SV_LinkEdict (check, false);
						SV_RecordPusherSupport (check, pusher, move);
						continue;
					}
					VectorCopy (pushedorg, check->v.origin);
				}
			}

			if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
			{ // corpse
				check->v.mins[0] = check->v.mins[1] = 0;
				VectorCopy (check->v.mins, check->v.maxs);
				continue;
			}

			// try moving the entity up a bit if it's blocked by the pusher while also standing on it
			if (!robust_push && riding && block == pusher &&
				(sv_gameplayfix_elevators.value >= 2.f || (sv_gameplayfix_elevators.value && NUM_FOR_EDICT (check) <= svs.maxclients)))
			{
				check->v.origin[2] += DIST_EPSILON;
				if (!SV_TestEntityPosition (check))
					continue;
			}

			VectorCopy (entorig, check->v.origin);
			SV_LinkEdict (check, true);

			if (!pusher->free)
			{
				VectorCopy (pushorig, pusher->v.origin);
				SV_LinkEdict (pusher, false);
				pusher->v.ltime -= movetime;

				// if the pusher has a "blocked" function, call it
				// otherwise, just stay in place until the obstacle is gone
				if (!check->free && pusher->v.blocked)
				{
					pr_global_struct->self = EDICT_TO_PROG (pusher);
					pr_global_struct->other = EDICT_TO_PROG (check);
					PR_ExecuteProgram (pusher->v.blocked);
				}
			}

			// move back any entities we already moved
			for (i = 0; i < num_moved; i++)
			{
				if (moved_edict[i]->free)
					continue;
				SV_RestorePusherSupport (moved_edict[i], &moved_support[i]);
				VectorCopy (moved_from[i], moved_edict[i]->v.origin);
				SV_LinkEdict (moved_edict[i], false);
			}
			break;
		}

		if (riding)
			SV_RecordPusherSupport (check, pusher, move);
	}

	for (i = num_moved - 1; i >= 0; i--)
		ED_Release (moved_edict[i]);
	for (i = num_push - 1; i >= 0; i--)
		ED_Release (push_edict[i]);
	ED_Release (pusher);

	TEMP_FREE (rider_ignore_storage);
	TEMP_FREE (push_candidates);
	TEMP_FREE (push_riding);
	TEMP_FREE (push_edict);
	TEMP_FREE (moved_support);
	TEMP_FREE (moved_from);
	TEMP_FREE (moved_edict);
}

/*
================
SV_Physics_Pusher

================
*/
static void SV_Physics_Pusher (edict_t *ent)
{
	float	 thinktime;
	float	 oldltime;
	float	 movetime;
	double	 push_start = 0;
	qboolean timing;

	oldltime = ent->v.ltime;

	thinktime = ent->v.nextthink;
	movetime = SV_PusherMoveTimeThisFrame (ent);

	timing = sv_speeds.value && qcvm == &sv.qcvm && (movetime || thinktime > oldltime);
	if (timing)
		push_start = Sys_DoubleTime ();

	if (movetime)
	{
		SV_PushMove (ent, movetime); // advances ent->v.ltime if not blocked
	}

	if (!ent->free && thinktime > oldltime && thinktime <= ent->v.ltime)
	{
		ent->v.nextthink = 0;
		pr_global_struct->time = qcvm->time;
		pr_global_struct->self = EDICT_TO_PROG (ent);
		pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
		ED_Retain (ent);
		qboolean friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
		PR_ExecuteProgram (ent->v.think);
		if (friendly_fire_scope)
			SV_CoopFriendlyFireEnd ();
		ED_Release (ent);
	}

	if (timing)
	{
		sv_speeds_pusher_ms += (Sys_DoubleTime () - push_start) * 1000.0;
		sv_speeds_pushers++;
	}
}

/*
===============================================================================

CLIENT MOVEMENT

===============================================================================
*/

/*
=============
SV_CheckStuck

This is a big hack to try and fix the rare case of getting stuck in the world
clipping hull.
=============
*/
static void SV_CheckStuck (edict_t *ent)
{
	int	   i, j;
	int	   z;
	vec3_t org;

	if (!SV_TestEntityPosition (ent))
	{
		VectorCopy (ent->v.origin, ent->v.oldorigin);
		return;
	}

	VectorCopy (ent->v.origin, org);
	VectorCopy (ent->v.oldorigin, ent->v.origin);
	if (!SV_TestEntityPosition (ent))
	{
		Con_DPrintf ("Unstuck.\n");
		SV_LinkEdict (ent, true);
		return;
	}

	for (z = 0; z < 18; z++)
		for (i = -1; i <= 1; i++)
			for (j = -1; j <= 1; j++)
			{
				ent->v.origin[0] = org[0] + i;
				ent->v.origin[1] = org[1] + j;
				ent->v.origin[2] = org[2] + z;
				if (!SV_TestEntityPosition (ent))
				{
					Con_DPrintf ("Unstuck.\n");
					SV_LinkEdict (ent, true);
					return;
				}
			}

	VectorCopy (org, ent->v.origin);
	Con_DPrintf ("player is stuck.\n");
}

static void SV_CheckStuckWithMoveFrame (edict_t *ent, const sv_client_move_frame_t *move_frame)
{
	if (!SV_ClientMoveFrameHasGroundSupport (move_frame))
	{
		SV_CheckStuck (ent);
		return;
	}

	if (!SV_TestEntityPosition (ent))
	{
		VectorCopy (ent->v.origin, ent->v.oldorigin);
		return;
	}

	if (!SV_EntityPositionBlockedIgnoringPusher (ent, move_frame->pusher))
	{
		VectorCopy (ent->v.origin, ent->v.oldorigin);
		return;
	}

	SV_CheckStuck (ent);
}

/*
=============
SV_CheckWater
=============
*/
static qboolean SV_CheckWater (edict_t *ent)
{
	vec3_t point;
	int	   cont;

	point[0] = ent->v.origin[0];
	point[1] = ent->v.origin[1];
	point[2] = ent->v.origin[2] + ent->v.mins[2] + 1;

	ent->v.waterlevel = 0;
	ent->v.watertype = CONTENTS_EMPTY;
	cont = SV_PointContents (point);
	if (cont <= CONTENTS_WATER)
	{
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		point[2] = ent->v.origin[2] + (ent->v.mins[2] + ent->v.maxs[2]) * 0.5;
		cont = SV_PointContents (point);
		if (cont <= CONTENTS_WATER)
		{
			ent->v.waterlevel = 2;
			point[2] = ent->v.origin[2] + ent->v.view_ofs[2];
			cont = SV_PointContents (point);
			if (cont <= CONTENTS_WATER)
				ent->v.waterlevel = 3;
		}
	}

	return ent->v.waterlevel > 1;
}

/*
============
SV_WallFriction

============
*/
static void SV_WallFriction (edict_t *ent, trace_t *trace)
{
	vec3_t forward, right, up;
	float  d, i;
	vec3_t into, side;

	AngleVectors (ent->v.v_angle, forward, right, up);
	d = DotProduct (trace->plane.normal, forward);

	d += 0.5;
	if (d >= 0)
		return;

	// cut the tangential velocity
	i = DotProduct (trace->plane.normal, ent->v.velocity);
	VectorScale (trace->plane.normal, i, into);
	VectorSubtract (ent->v.velocity, into, side);

	ent->v.velocity[0] = side[0] * (1 + d);
	ent->v.velocity[1] = side[1] * (1 + d);
}

/*
=====================
SV_TryUnstick

Player has come to a dead stop, possibly due to the problem with limited
float precision at some angle joins in the BSP hull.

Try fixing by pushing one pixel in each direction.

This is a hack, but in the interest of good gameplay...
======================
*/
static int SV_TryUnstick (edict_t *ent, vec3_t oldvel, qboolean callbacks)
{
	int		i;
	vec3_t	oldorg;
	vec3_t	dir, dest;
	int		clip;
	trace_t steptrace;

	VectorCopy (ent->v.origin, oldorg);
	VectorCopy (vec3_origin, dir);

	for (i = 0; i < 8; i++)
	{
		// try pushing a little in an axial direction
		switch (i)
		{
		case 0:
			dir[0] = 2;
			dir[1] = 0;
			break;
		case 1:
			dir[0] = 0;
			dir[1] = 2;
			break;
		case 2:
			dir[0] = -2;
			dir[1] = 0;
			break;
		case 3:
			dir[0] = 0;
			dir[1] = -2;
			break;
		case 4:
			dir[0] = 2;
			dir[1] = 2;
			break;
		case 5:
			dir[0] = -2;
			dir[1] = 2;
			break;
		case 6:
			dir[0] = 2;
			dir[1] = -2;
			break;
		case 7:
			dir[0] = -2;
			dir[1] = -2;
			break;
		}

		VectorAdd (ent->v.origin, dir, dest);
		SV_PushEntityTo (ent, dest, callbacks);

		// retry the original move
		ent->v.velocity[0] = oldvel[0];
		ent->v.velocity[1] = oldvel[1];
		ent->v.velocity[2] = 0;
		clip = SV_FlyMove (ent, 0.1, NULL, &steptrace, callbacks);

		if (fabs (oldorg[1] - ent->v.origin[1]) > 4 || fabs (oldorg[0] - ent->v.origin[0]) > 4)
		{
			// Con_DPrintf ("unstuck!\n");
			return clip;
		}

		// go back to the original pos and try again
		VectorCopy (oldorg, ent->v.origin);
	}

	VectorCopy (vec3_origin, ent->v.velocity);
	return 7; // still not moving
}

/*
=====================
SV_WalkMove

Only used by players
======================
*/
static void SV_WalkMove (edict_t *ent, const sv_client_move_frame_t *move_frame, const vec3_t move_velocity, qboolean callbacks)
{
	vec3_t	upmove, downmove;
	vec3_t	oldorg, oldvel;
	vec3_t	nosteporg, nostepvel;
	int		clip;
	int		oldonground;
	trace_t steptrace, downtrace;

	//
	// do a regular slide move unless it looks like you ran into a step
	//
	oldonground = (int)ent->v.flags & FL_ONGROUND;
	ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

	VectorCopy (ent->v.origin, oldorg);
	VectorCopy (ent->v.velocity, oldvel);

	clip = SV_FlyMoveWithMoveFrameClipContext (ent, host_frametime, move_frame, move_velocity, &steptrace, callbacks);

	if (!(clip & 2))
	{
		SV_GroundClientOnMoveFramePusher (ent, move_frame);
		return; // move didn't block on a step
	}

	if (!oldonground && ent->v.waterlevel == 0)
		return; // don't stair up while jumping

	if (ent->v.movetype != MOVETYPE_WALK)
		return; // gibbed by a trigger

	if (sv_nostep.value)
		return;

	if ((int)ent->v.flags & FL_WATERJUMP)
		return;

	VectorCopy (ent->v.origin, nosteporg);
	VectorCopy (ent->v.velocity, nostepvel);

	//
	// try moving up and forward to go up a step
	//
	VectorCopy (oldorg, ent->v.origin); // back to start pos

	VectorCopy (ent->v.origin, upmove);
	upmove[2] += STEPSIZE;

	// move up
	SV_PushEntityTo (ent, upmove, callbacks); // FIXME: don't link?

	// move forward
	ent->v.velocity[0] = oldvel[0];
	ent->v.velocity[1] = oldvel[1];
	ent->v.velocity[2] = 0;
	clip = SV_FlyMoveWithMoveFrameClipContext (ent, host_frametime, move_frame, NULL, &steptrace, callbacks);

	// check for stuckness, possibly due to the limited precision of floats
	// in the clipping hulls. Disable when using pr_checkextension to avoid
	// https://github.com/Shpoike/Quakespasm/issues/50.
	if (clip && !pr_checkextension.value)
	{
		if (fabs (oldorg[1] - ent->v.origin[1]) < 0.03125 && fabs (oldorg[0] - ent->v.origin[0]) < 0.03125)
		{ // stepping up didn't make any progress
			clip = SV_TryUnstick (ent, oldvel, callbacks);
		}
	}

	// extra friction based on view angle
	if (clip & 2)
		SV_WallFriction (ent, &steptrace);

	// move down
	VectorCopy (ent->v.origin, downmove);
	downmove[2] += -STEPSIZE + move_velocity[2] * host_frametime;
	downtrace = SV_PushEntityTo (ent, downmove, callbacks); // FIXME: don't link?

	if (downtrace.plane.normal[2] > MIN_WALK_NORMAL)
	{
		if (ent->v.solid == SOLID_BSP || (SV_ClientMoveFrameHasGroundSupport (move_frame) && downtrace.ent == move_frame->pusher))
		{
			ent->v.flags = (int)ent->v.flags | FL_ONGROUND;

			// Native pushes can touch triggers and free downtrace.ent.
			if (downtrace.ent && !downtrace.ent->free)
				ent->v.groundentity = EDICT_TO_PROG (downtrace.ent);
		}
	}
	else
	{
		// if the push down didn't end up on good ground, use the move without
		// the step up.  This happens near wall / slope combinations, and can
		// cause the player to hop up higher on a slope too steep to climb
		VectorCopy (nosteporg, ent->v.origin);
		VectorCopy (nostepvel, ent->v.velocity);
		SV_GroundClientOnMoveFramePusher (ent, move_frame);
	}
}

/*
================
SV_Physics_Client

Player character actions
================
*/
static void SV_Physics_ClientWalk (edict_t *ent, sv_client_move_frame_t *move_frame,
	qboolean gorilla_braced)
{
	vec3_t	 move_velocity, old_velocity;
	qboolean supported_by_pusher;
	qboolean in_water;
	qboolean apply_gravity;

	supported_by_pusher = SV_ClientMoveFrameHasGroundSupport (move_frame);
	in_water = SV_CheckWater (ent);
	apply_gravity = !gorilla_braced && !supported_by_pusher && !in_water &&
		!((int)ent->v.flags & FL_WATERJUMP);

	if (apply_gravity)
		SV_AddGravity (ent, move_velocity);
	else
	{
		if (supported_by_pusher)
			ent->v.velocity[2] = 0;
		VectorCopy (ent->v.velocity, move_velocity);
	}

	VectorCopy (ent->v.velocity, old_velocity);
	SV_CheckStuckWithMoveFrame (ent, move_frame);
	assert_always (!ent->free);
	// Unsticking can touch a trigger that replaces the velocity.
	if (!VectorCompare (ent->v.velocity, old_velocity))
		VectorCopy (ent->v.velocity, move_velocity);
	SV_WalkMove (ent, move_frame, move_velocity, true);
}

/* Physical head movement is an auxiliary translation, not a second player
 * think. Use the same collision/step solver, but let the normal physics pass
 * own velocity, ground state and QuakeC callbacks. */
static void SV_ApplyPrivateRoomScaleMove (edict_t *ent, client_t *client)
{
	sv_client_move_frame_t auxiliary_frame;
	vec3_t move, sweep_velocity, saved_velocity;
	float saved_flags;
	int saved_groundentity;

	VectorCopy (client->cmd.vr_roomscalemove, move);
	VectorCopy (vec3_origin, client->cmd.vr_roomscalemove);
	if (!client->cmd.vr_active || ent->v.movetype == MOVETYPE_NONE ||
		host_frametime <= 0)
		return;
	move[2] = 0; // head height does not raise the player's collision hull
	if (!move[0] && !move[1])
		return;

	VectorCopy (ent->v.velocity, saved_velocity);
	saved_flags = ent->v.flags;
	saved_groundentity = ent->v.groundentity;
	VectorScale (move, 1.0f / host_frametime, sweep_velocity);
	VectorCopy (sweep_velocity, ent->v.velocity);
	if (ent->v.movetype == MOVETYPE_NOCLIP)
		VectorAdd (ent->v.origin, move, ent->v.origin);
	else if (ent->v.movetype == MOVETYPE_WALK)
	{
		SV_CaptureClientMoveFrameBeforeQC (ent, &auxiliary_frame);
		SV_WalkMove (ent, &auxiliary_frame, sweep_velocity, false);
	}
	else
		SV_FlyMove (ent, host_frametime, NULL, NULL, false);
	VectorCopy (saved_velocity, ent->v.velocity);
	ent->v.flags = saved_flags;
	ent->v.groundentity = saved_groundentity;
	SV_LinkEdict (ent, false);
}

/* Source instant stop precedes PlayerPreThink. Do it here for the selected
 * private owner so a subsequent QuakeC velocity write survives PMove. */
static qboolean SV_PrivateInstantStopHasGround (edict_t *ent)
{
	vec3_t point, gravitydir = {0, 0, -1}, bounce;
	trace_t trace;

	/* The room-scale sweep restores the previous ground flag. Prediction
	 * categorizes at the swept position, so check that same one-unit support
	 * before allowing the server's pre-QuakeC stop. */
	if (ent->v.velocity[2] > 180.0f)
		return false;
	VectorCopy (ent->v.origin, point);
	point[2] -= 1.0f;
	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs,
		point, MOVE_NORMAL, ent);
	if (!trace.startsolid && trace.fraction < 1.0f &&
		trace.plane.normal[2] < 0.7f)
	{
		/* Match PM_CategorizePosition's second trace at a slope base. */
		ClipVelocity (gravitydir, trace.plane.normal, bounce, 2.0f);
		VectorMA (trace.endpos, 1.0f - trace.fraction, bounce, point);
		trace = SV_Move (trace.endpos, ent->v.mins, ent->v.maxs,
			point, MOVE_NORMAL, ent);
	}
	return !trace.startsolid && trace.fraction < 1.0f &&
		trace.plane.normal[2] >= 0.7f;
}

static void SV_PrivateInstantStopBeforeQC (edict_t *ent, client_t *client,
	const usercmd_t *command, qboolean enabled, qboolean pground)
{
	if (!enabled || !ent || !client || !command || !command->vr_active ||
		(int)ent->v.movetype != MOVETYPE_WALK ||
		(pground && !((int)ent->v.flags & FL_ONGROUND)) ||
		((int)ent->v.flags & FL_WATERJUMP) ||
		client->private_pmove_waterjump_secs > 0.0f ||
		ent->v.waterlevel >= 2 || ent->v.watertype == CONTENTS_LADDER ||
		SV_GorillaNativeLadder (ent) ||
		!PM_VRInstantStopNeutralInput (command,
			qcvm->time < ent->v.teleport_time))
		return;
	if (client->vr_gorilla_capable && sv_gorilla.value &&
		(((command->vr_gorilla.flags & VR_GORILLA_HANDS) == VR_GORILLA_HANDS) ||
		 (command->vr_gorilla_motion.flags & VR_GORILLA_MOTION_ACTIVE)))
		return;
	if (!SV_PrivateInstantStopHasGround (ent))
		return;
	ent->v.velocity[0] = 0.0f;
	ent->v.velocity[1] = 0.0f;
}

typedef struct sv_vr_weapon_pose_scope_s
{
	struct sv_vr_weapon_pose_scope_s *previous;
	edict_t *ent;
	client_t *client;
	qboolean applied, origin_relocated, linked, akimbo_invalidated;
	qboolean akimbo_pose_valid;
	/* Set only by a fully admitted Dwell pair path. */
	qboolean dwell_berserk_pose_valid;
	qboolean enyo_clearance_pending;
	qboolean qbj3_shotgun_spread;
	qboolean stock_id1_muzzle_valid;
	qboolean stock_lightning_trace_applied;
	qboolean stock_lightning_damage_started;
	int stock_id1_program; /* 0 unchecked, 1 pinned id1, -1 other */
	int qbj3_shotgun_weapon;
	float qbj3_shotgun_roll;
	vec3_t origin, body_origin, v_angle, forward, right, up;
	vec3_t stock_id1_muzzle;
	vec3_t stock_lightning_end;
	vec3_t stock_lightning_damage_end;
	vec3_t akimbo_muzzle[2], akimbo_angles[2];
	vec3_t enyo_clearance_start, enyo_clearance_end;
	vec3_t enyo_clearance_adjusted_start;
	float enyo_clearance_t0;
} sv_vr_weapon_pose_scope_t;

static sv_vr_weapon_pose_scope_t *sv_vr_weapon_pose_scope;
static void SV_VRContactInvalidateAccepted (client_t *client);

typedef enum
{
	SV_VR_AXE_TRACE_SCOPE_NONE,
	SV_VR_AXE_TRACE_SCOPE_STOCK,
	SV_VR_AXE_TRACE_SCOPE_DWELL
} sv_vr_axe_trace_scope_mode_t;

/* One native axe call may borrow one already validated physical trace. The
 * stock and Dwell paths share this owner; their QC acquisition rules stay
 * separate and are pinned again at the builtin boundary. */
static struct
{
	sv_vr_axe_trace_scope_mode_t mode;
	client_t *client;
	edict_t *player;
	const dfunction_t *function;
	trace_t trace;
	qboolean active, dwell_force_miss, dwell_has_contact, dwell_invalidated;
} sv_vr_axe_trace_scope;

void SV_ClearVRWeaponPoseScope (void)
{
	/* Host_Error/EndGame can unwind a QC callback past its normal restore. */
	sv_vr_weapon_pose_scope = NULL;
}

void SV_VRWeaponPoseSetOrigin (edict_t *ent)
{
	sv_vr_weapon_pose_scope_t *scope;
	if (qcvm != &sv.qcvm)
		return;
	for (scope = sv_vr_weapon_pose_scope; scope; scope = scope->previous)
		if (scope->ent == ent)
		{
			scope->origin_relocated = true;
			scope->akimbo_invalidated = true;
			scope->akimbo_pose_valid = false;
			scope->enyo_clearance_pending = false;
			scope->stock_id1_muzzle_valid = false;
			scope->stock_lightning_trace_applied = false;
			scope->stock_lightning_damage_started = false;
		}
}

void SV_VRWeaponPoseLinked (edict_t *ent)
{
	sv_vr_weapon_pose_scope_t *scope;
	for (scope = sv_vr_weapon_pose_scope; scope; scope = scope->previous)
		if (scope->ent == ent)
			scope->linked = true;
}

static void SV_ClampVRMuzzleToWorld (edict_t *ent, vec3_t muzzle)
{
	vec3_t start, delta;
	trace_t trace;
	int i;
	VectorAdd (ent->v.origin, ent->v.view_ofs, start);
	for (i = 0; i < 3; i++)
		if (!isfinite (muzzle[i]))
		{
			VectorCopy (start, muzzle);
			return;
		}
	VectorSubtract (muzzle, start, delta);
	if (VectorLength (delta) > 512.0f)
	{
		VectorCopy (start, muzzle);
		return;
	}
	trace = SV_Move (start, vec3_origin, vec3_origin, muzzle,
		MOVE_NOMONSTERS, ent);
	if (trace.startsolid || trace.allsolid)
		VectorCopy (start, muzzle);
	else if (trace.fraction < 1.0f)
	{
		VectorCopy (trace.endpos, muzzle);
		if (VectorNormalize (delta) > 0.0f)
			VectorMA (muzzle, -1.0f, delta, muzzle);
	}
}

#define SV_VR_AKIMBO_MAX_MUZZLE_OFFSET 96.0f
#define SV_VR_AKIMBO_MAX_ANGLE 3600.0f
#define SV_VR_AKIMBO_MAX_FRESHNESS 0.25

qboolean SV_QBJ3TwinNailgunProgramLoaded (void)
{
	static const byte expected_sha256[32] = {
		0xde, 0x2c, 0x6a, 0x60, 0xdf, 0x24, 0xf5, 0xce,
		0x0c, 0x3f, 0xc4, 0x1b, 0x0f, 0xd6, 0x30, 0x91,
		0x05, 0xa0, 0xea, 0x7a, 0xe8, 0x95, 0xdf, 0xb4,
		0xa6, 0x86, 0x79, 0x50, 0xb9, 0xb9, 0x0e, 0x34
	};

	return qcvm == &sv.qcvm && qcvm->progssize == 905470 &&
		!memcmp (qcvm->progssha256, expected_sha256, sizeof (expected_sha256)) &&
		!strcmp (COM_SkipPath (com_gamedir), "qbj3");
}

#define ENYO_PROGS_SIZE 823998
#define ENYO_W_FIRESMG_FIRST_STATEMENT 15323
#define ENYO_W_FIRESMG_PARM_START 7335
#define ENYO_W_FIRESMG_MAKEVECTORS_STATEMENT 15324
#define ENYO_W_FIRESMG_AIM_STATEMENT 15344
#define ENYO_W_FIRESMG_TRACELINE_STATEMENT 15352

static qboolean SV_EnyoSMGFunction (const dfunction_t *function)
{
	return function && !strcmp (PR_GetString (function->s_name), "W_FireSMG") &&
		function->first_statement == ENYO_W_FIRESMG_FIRST_STATEMENT &&
		function->parm_start == ENYO_W_FIRESMG_PARM_START &&
		function->locals == 7 && function->numparms == 1 &&
		function->parm_size[0] == 1;
}

qboolean SV_EnyoAkimboProgramLoaded (void)
{
	static const byte expected_sha256[32] = {
		0xb0, 0xd3, 0x86, 0x5f, 0x11, 0x92, 0xb3, 0x85,
		0x8e, 0x74, 0x10, 0xea, 0x31, 0xcb, 0xc8, 0x2d,
		0x88, 0x13, 0x6e, 0x63, 0x5b, 0x9c, 0x1d, 0x13,
		0xd0, 0xaf, 0xf9, 0xbb, 0xed, 0xda, 0xeb, 0x1e
	};
	dfunction_t *function;

	if (qcvm != &sv.qcvm || q_strcasecmp (COM_SkipPath (com_gamedir), "enyo") ||
		qcvm->progssize != ENYO_PROGS_SIZE ||
		memcmp (qcvm->progssha256, expected_sha256, sizeof (expected_sha256)))
		return false;
	function = ED_FindFunction ("W_FireSMG");
	return SV_EnyoSMGFunction (function);
}

#define DWELL_PROGS_SIZE 820938
#define DWELL_BERSERK_FINISHED_OFS 151
#define DWELL_NUMSTATEMENTS 57592
#define DWELL_NUMFUNCTIONS 4068
#define DWELL_NUMGLOBALS 10026
#define DWELL_HAS_HASTE_FUNCTION 129
#define DWELL_TRACELINE2_FUNCTION 396
#define DWELL_SUPER_DAMAGE_SOUND_FUNCTION 416
#define DWELL_BERSERK_SOUND_FUNCTION 417
#define DWELL_AXE_WHIFF_SOUND_FUNCTION 432
#define DWELL_W_FIREAXE_FUNCTION 438
#define DWELL_PLAYER_STAND_FUNCTION 549
#define DWELL_PLAYER_RUN_FUNCTION 550
#define DWELL_W_FIREAXE_FIRST_STATEMENT 14560
#define DWELL_W_FIREAXE_MAKEVECTORS_STATEMENT 14565
#define DWELL_W_FIREAXE_PARM_START 7805

static qboolean SV_DwellFunctionPin (int index, const char *name,
	int first_statement, int parm_start, int locals, int numparms,
	const byte *parm_sizes)
{
	dfunction_t *function;
	const char *function_name;
	int i;

	if (!qcvm || !qcvm->progs || !qcvm->functions || index < 0 ||
		index >= qcvm->progs->numfunctions || numparms < -1 ||
		numparms > MAX_PARMS)
		return false;
	function = &qcvm->functions[index];
	if (function->numparms < 0 || function->numparms > MAX_PARMS ||
		function->locals < 0 || function->parm_start < 0 ||
		function->locals > qcvm->progs->numglobals ||
		function->parm_start > qcvm->progs->numglobals - function->locals ||
		function->numparms > function->locals)
		return false;
	function_name = PR_GetString (function->s_name);
	if (strcmp (function_name, name) ||
		function->first_statement != first_statement ||
		(parm_start >= 0 && function->parm_start != parm_start) ||
		(locals >= 0 && function->locals != locals) ||
		(numparms >= 0 && function->numparms != numparms))
		return false;
	if (numparms >= 0)
	{
		int total_parm_size = 0;

		for (i = 0; i < numparms; ++i)
		{
			if (parm_sizes && function->parm_size[i] != parm_sizes[i])
				return false;
			total_parm_size += function->parm_size[i];
		}
		if (total_parm_size > function->locals)
			return false;
	}
	return true;
}

/* The installed QBJ3 revision is already identified by the shared exact
 * progs hash. Pin the entries the physical outcome borrows or admits. Native
 * scheduled attacks keep their original roots and fan traces. */
static qboolean SV_QBJ3MeleeProgramLoaded (void)
{
	static const byte leaf_parms[] = {1, 3, 3};

	return SV_QBJ3TwinNailgunProgramLoaded () && qcvm->progs &&
		SV_DwellFunctionPin (485, "hitwrench", 15218, 8411, 11, 3,
			leaf_parms) &&
		SV_DwellFunctionPin (572, "hit_berserker_punch", 19201, 8739,
			11, 3, leaf_parms) &&
		SV_DwellFunctionPin (565, "weaponanim_berserk_loop", 18858,
			0, 0, 0, NULL) &&
		SV_DwellFunctionPin (571, "W_Fire_Berserker_Multi", 18996,
			8722, 17, 0, NULL) &&
		SV_DwellFunctionPin (595, "SuperDamageSound", 20453, 0, 0, 0,
			NULL) &&
		SV_DwellFunctionPin (569, "weaponanim_idle_melee_loop", 18942,
			0, 0, 0, NULL) &&
		SV_DwellFunctionPin (575, "weaponanim_draw_loop", 19624,
			0, 0, 0, NULL);
}

unsigned int SV_VRQBJ3MeleeContactProfile (void)
{
	return SV_QBJ3MeleeProgramLoaded () ?
		VR_WEAPON_CONTACT_PROFILE_QBJ3 : VR_WEAPON_CONTACT_PROFILE_NONE;
}

/* Donor vr_melee_qc.h:SV_VRMeleeEnyoProgs, using the already loaded exact
 * Enyo hash. The native hitsword leaf owns damage, healing and aftermath. */
static qboolean SV_EnyoMeleeProgramLoaded (void)
{
	static const byte scalar[] = {1}, leaf_parms[] = {1, 3, 3};
	return SV_EnyoAkimboProgramLoaded () && qcvm->progs &&
		qcvm->progs->numstatements == 60325 &&
		qcvm->progs->numfunctions == 3607 && qcvm->progs->numglobals == 9635 &&
		SV_DwellFunctionPin (399, "W_SetCurrentAmmo", 15623, 7368, 1, 1, scalar) &&
		SV_DwellFunctionPin (404, "W_Attack", 15927, 0, 0, 0, NULL) &&
		SV_DwellFunctionPin (346, "W_FireSword", 12963, 7000, 17, 0, NULL) &&
		SV_DwellFunctionPin (347, "hitsword", 13148, 7017, 7, 3, leaf_parms) &&
		SV_DwellFunctionPin (551, "player_sword1", 22098, 7491, 3, 0, NULL) &&
		SV_DwellFunctionPin (554, "player_sword4", 22145, 0, 0, 0, NULL);
}

unsigned int SV_VREnyoMeleeContactProfile (void)
{
	return SV_EnyoMeleeProgramLoaded () ?
		VR_WEAPON_CONTACT_PROFILE_ENYO : VR_WEAPON_CONTACT_PROFILE_NONE;
}

/* QBJ3's native has_berserk tests the item bit or the float QC expiry. The
 * model and weapon bit must still be the currently selected melee weapon. */
static qboolean SV_VRQBJ3MeleeSelected (edict_t *ent, qboolean *berserk)
{
	eval_t *items, *finished;
	const char *model;
	float qctime;
	int bits;

	if (!ent || ent->free || !berserk || !SV_QBJ3MeleeProgramLoaded () ||
		!isfinite (ent->v.weapon) || ent->v.weapon != 4096 ||
		!isfinite (qcvm->time))
		return false;
	qctime = (float)qcvm->time;
	if (!isfinite (qctime))
		return false;
	items = GetEdictFieldValue (ent, ED_FindFieldOffset ("items_qbj"));
	finished = GetEdictFieldValue (ent,
		ED_FindFieldOffset ("berserk_finished"));
	if (!items || !finished || !isfinite (items->_float) ||
		(double)items->_float < -2147483648.0 ||
		(double)items->_float >= 2147483648.0 ||
		!isfinite (finished->_float))
		return false;
	bits = (int)items->_float;
	*berserk = (bits & 4) != 0 || finished->_float > qctime;
	model = PR_GetString (ent->v.weaponmodel);
	return model && !strcmp (model, *berserk ? "progs/v_berserk.mdl" :
		"progs/v_wrench.mdl");
}

/* Internal stroke identity; never serialized or inferred from a model alone. */
enum
{
	SV_VR_DIRECT_MELEE_NONE,
	SV_VR_DIRECT_MELEE_QBJ3_WRENCH,
	SV_VR_DIRECT_MELEE_QBJ3_BERSERK,
	SV_VR_DIRECT_MELEE_ENYO_SWORD
};

static qboolean SV_VRDirectMeleeSelected (edict_t *ent, int *subtype)
{
	qboolean berserk;
	if (!subtype)
		return false;
	*subtype = SV_VR_DIRECT_MELEE_NONE;
	if (SV_VRQBJ3MeleeSelected (ent, &berserk))
		*subtype = berserk ? SV_VR_DIRECT_MELEE_QBJ3_BERSERK :
			SV_VR_DIRECT_MELEE_QBJ3_WRENCH;
	else if (ent && !ent->free && ent->v.weapon == 4096 &&
		SV_EnyoMeleeProgramLoaded () &&
		!strcmp (PR_GetString (ent->v.weaponmodel), "progs/ee_v_sword.mdl"))
		*subtype = SV_VR_DIRECT_MELEE_ENYO_SWORD;
	return *subtype != SV_VR_DIRECT_MELEE_NONE;
}

qboolean SV_DwellBerserkAkimboProgramLoaded (void)
{
	static const byte has_haste_parm_sizes[] = {1};
	static const byte traceline2_parm_sizes[] = {3, 3, 1, 1};
	static const byte expected_sha256[32] = {
		0xfe, 0x7d, 0x21, 0xd4, 0xbd, 0xfd, 0x1a, 0x5e,
		0x66, 0x72, 0xd1, 0x60, 0x6e, 0xfd, 0x77, 0x4c,
		0xb7, 0x30, 0xd7, 0xf5, 0xb6, 0x89, 0x41, 0xd8,
		0x21, 0x75, 0x59, 0x6f, 0x22, 0xe7, 0x19, 0xfd
	};
	const char *game;
	ddef_t *finished;
	dfunction_t *function;

	if (qcvm != &sv.qcvm || !qcvm->progs)
		return false;
	game = COM_SkipPath (com_gamedir);
	if (!game || (q_strcasecmp (game, "dwell") &&
		q_strcasecmp (game, "dwellv2p2")) ||
		qcvm->progssize != DWELL_PROGS_SIZE ||
		memcmp (qcvm->progssha256, expected_sha256, sizeof (expected_sha256)))
		return false;
	if (qcvm->progs->numstatements != DWELL_NUMSTATEMENTS ||
		qcvm->progs->numfunctions != DWELL_NUMFUNCTIONS ||
		qcvm->progs->numglobals != DWELL_NUMGLOBALS ||
		!SV_DwellFunctionPin (DWELL_HAS_HASTE_FUNCTION, "has_haste",
			4038, 7022, 1, 1, has_haste_parm_sizes) ||
		!SV_DwellFunctionPin (DWELL_TRACELINE2_FUNCTION, "traceline2",
			12883, 7674, 24, 4, traceline2_parm_sizes) ||
		!SV_DwellFunctionPin (DWELL_SUPER_DAMAGE_SOUND_FUNCTION,
			"SuperDamageSound", 13513, 0, 0, 0, NULL) ||
		!SV_DwellFunctionPin (DWELL_BERSERK_SOUND_FUNCTION,
			"BerserkSound", 13535, 0, 0, 0, NULL) ||
		!SV_DwellFunctionPin (DWELL_AXE_WHIFF_SOUND_FUNCTION,
			"W_AxeWhiffSound", 14468, 0, 0, 0, NULL) ||
		!SV_DwellFunctionPin (DWELL_W_FIREAXE_FUNCTION, "W_FireAxe",
			DWELL_W_FIREAXE_FIRST_STATEMENT, DWELL_W_FIREAXE_PARM_START,
			9, 0, NULL) ||
		!SV_DwellFunctionPin (DWELL_PLAYER_STAND_FUNCTION,
			"player_stand1", 20531, 0, 0, 0, NULL) ||
		!SV_DwellFunctionPin (DWELL_PLAYER_RUN_FUNCTION,
			"player_run", 20567, 0, 0, 0, NULL))
		return false;

	finished = ED_FindField ("berserk_finished");
	if (!finished || (finished->type & ~DEF_SAVEGLOBAL) != ev_float ||
		finished->ofs != DWELL_BERSERK_FINISHED_OFS)
		return false;
	function = ED_FindFunction ("W_FireAxe");
	return function && function == &qcvm->functions[DWELL_W_FIREAXE_FUNCTION];
}

qboolean SV_DwellBerserkAkimboWeaponSelected (edict_t *ent)
{
	eval_t *finished;
	const char *weaponmodel;
	float qctime;

	if (!ent || ent->free || !SV_DwellBerserkAkimboProgramLoaded () ||
		!isfinite (ent->v.weapon) || ent->v.weapon != 4096 ||
		!isfinite (qcvm->time))
		return false;
	qctime = (float)qcvm->time;
	if (!isfinite (qctime))
		return false;
	weaponmodel = PR_GetString (ent->v.weaponmodel);
	if (!weaponmodel || strcmp (weaponmodel, "progs/v_axeb.mdl"))
		return false;
	finished = GetEdictFieldValue (ent,
		ED_FindFieldOffset ("berserk_finished"));
	return finished && isfinite (finished->_float) &&
		finished->_float > qctime;
}

static qboolean SV_EnyoSMGWeapon (edict_t *ent)
{
	return ent && !ent->free && SV_EnyoAkimboProgramLoaded () &&
		ent->v.weapon == 4 &&
		!strcmp (PR_GetString (ent->v.weaponmodel), "progs/ee_v_smgs.mdl");
}

static qboolean SV_EnyoVectorIsFinite (const vec3_t value)
{
	return isfinite (value[0]) && isfinite (value[1]) && isfinite (value[2]);
}

/* Dwell weaponframe is source animation data. Controller indices are
 * anatomical: 0 is left and 1 is right. */
static qboolean SV_DwellBerserkStrikeHand (float weaponframe, int *hand)
{
	static const char hands[] =
		"0000000000" "0110011111" "0100000000"
		"0111000000" "0111000111" "0";
	int frame;

	if (!hand || !isfinite (weaponframe) || weaponframe < 0.0f ||
		weaponframe > 50.0f)
		return false;
	frame = (int)weaponframe;
	if (weaponframe != (float)frame)
		return false;
	*hand = hands[frame] - '0';
	return true;
}

static qboolean SV_EnyoVectorsNear (const vec3_t a, const vec3_t b)
{
	vec3_t delta;
	VectorSubtract (a, b, delta);
	return DotProduct (delta, delta) <= 0.015625f;
}

static edict_t *SV_EnyoAkimboSelf (void)
{
	int self;
	if (qcvm != &sv.qcvm || !qcvm->edicts || qcvm->edict_size <= 0 ||
		!pr_global_struct)
		return NULL;
	self = pr_global_struct->self;
	if (self < 0 || self % qcvm->edict_size ||
		self / qcvm->edict_size >= qcvm->num_edicts)
		return NULL;
	return PROG_TO_EDICT (self);
}

static qboolean SV_AkimboCommandValid (client_t *client,
	const usercmd_t *cmd, const vec3_t body_origin)
{
	int hand, axis;
	qboolean qbj3_berserk = false;
	if (client && client->edict && cmd && cmd->vr_akimbo_berserk)
	{
		qboolean selected_berserk;
		qbj3_berserk = SV_VRQBJ3MeleeSelected (client->edict,
			&selected_berserk) && selected_berserk;
	}
	if (!client || !client->active || !client->spawned ||
		client->protocol_qsvr != QSVR_PROTOCOL_PINNED || !cmd ||
		!cmd->vr_active || !cmd->vr_handpos_relative ||
		!cmd->vr_akimbo_active ||
		(cmd->vr_akimbo_berserk &&
		 !qbj3_berserk &&
		 (!SV_VRDwellBerserkMeleeEnabled () ||
		  !SV_DwellBerserkAkimboProgramLoaded ())) ||
		cmd->sequence <= 0 || cmd->msec < 1 || cmd->msec > 125 ||
		!isfinite (cmd->vr_contact_received) || cmd->vr_contact_received < 0 ||
		realtime < cmd->vr_contact_received ||
		realtime - cmd->vr_contact_received > SV_VR_AKIMBO_MAX_FRESHNESS)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (body_origin[axis]) || fabsf (body_origin[axis]) > 1000000.0f)
			return false;

	for (hand = 0; hand < 2; hand++)
	{
		vec3_t offset;
		for (axis = 0; axis < 3; axis++)
		{
			if (!isfinite (cmd->vr_akimbo_muzzle[hand][axis]) ||
				!isfinite (cmd->vr_akimbo_angles[hand][axis]) ||
				fabsf (cmd->vr_akimbo_angles[hand][axis]) >
					SV_VR_AKIMBO_MAX_ANGLE)
				return false;
			offset[axis] = cmd->vr_akimbo_muzzle[hand][axis];
		}
		if (VectorLength (offset) > SV_VR_AKIMBO_MAX_MUZZLE_OFFSET)
			return false;
	}
	return true;
}

static qboolean SV_VRStockID1Program (sv_vr_weapon_pose_scope_t *scope)
{
	const sv_vr_stock_axe_descriptor_t *descriptor;
	if (!scope)
		return false;
	if (!scope->stock_id1_program)
	{
		descriptor = SV_VRStockAxeMeleeDescriptor ();
		scope->stock_id1_program = descriptor &&
			descriptor->progscrc == 3064 ? 1 : -1;
	}
	return scope->stock_id1_program > 0;
}

static void SV_BeginPrivateVRWeaponPose (edict_t *ent, client_t *client,
	const usercmd_t *cmd, sv_vr_weapon_pose_scope_t *scope)
{
	vec3_t muzzle, source_offset, flak_source_angles;
	qboolean qbj3_flak_source = false;
	sv_vr_weapon_pose_scope_t *previous;
	memset (scope, 0, sizeof (*scope));
	scope->ent = ent;
	scope->client = client;
	VectorCopy (ent->v.origin, scope->body_origin);
	for (previous = sv_vr_weapon_pose_scope; previous;
		previous = previous->previous)
		if (previous->ent == ent)
		{
			VectorCopy (previous->body_origin, scope->body_origin);
			scope->akimbo_invalidated = previous->akimbo_invalidated ||
				previous->origin_relocated;
			break;
		}
	/* Even a rejected nested entry must mask an older pose for this entity. */
	scope->previous = sv_vr_weapon_pose_scope;
	sv_vr_weapon_pose_scope = scope;
	if (client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!cmd->vr_active || !cmd->vr_handpos_relative ||
		client->lastmovetime <= 0 || realtime - client->lastmovetime > 1.0)
		return;

	scope->applied = true;
	VectorCopy (ent->v.origin, scope->origin);
	VectorCopy (ent->v.v_angle, scope->v_angle);
	VectorCopy (pr_global_struct->v_forward, scope->forward);
	VectorCopy (pr_global_struct->v_right, scope->right);
	VectorCopy (pr_global_struct->v_up, scope->up);

	if (!scope->akimbo_invalidated &&
		(SV_QBJ3TwinNailgunProgramLoaded () || SV_EnyoAkimboProgramLoaded () ||
		 (SV_VRDwellBerserkMeleeEnabled () &&
		  SV_DwellBerserkAkimboWeaponSelected (ent))) &&
		SV_AkimboCommandValid (client, cmd, scope->body_origin))
	{
		int hand, axis;
		for (hand = 0; hand < 2; hand++)
		{
			for (axis = 0; axis < 3; axis++)
				scope->akimbo_muzzle[hand][axis] = scope->body_origin[axis] +
					cmd->vr_akimbo_muzzle[hand][axis];
			VectorCopy (cmd->vr_akimbo_angles[hand],
				scope->akimbo_angles[hand]);
		}
		scope->akimbo_pose_valid = true;
		scope->dwell_berserk_pose_valid = cmd->vr_akimbo_berserk &&
			SV_VRDwellBerserkMeleeEnabled () &&
			SV_DwellBerserkAkimboWeaponSelected (ent);
	}
	/* Dwell's pinned makevectors site selects the striking hand later. QC
	 * before that site must continue to see the player's body pose. */
	if (scope->dwell_berserk_pose_valid)
		return;
	if (scope->akimbo_pose_valid && cmd->vr_akimbo_berserk)
	{
		qboolean berserk = false;
		if (SV_VRQBJ3MeleeSelected (ent, &berserk) && berserk &&
			(ent->v.weaponframe == 14 || ent->v.weaponframe == 34 ||
			 ent->v.weaponframe == 54 || ent->v.weaponframe == 64))
		{
			/* QBJ3 tests its striking frame before advancing the native
			 * animation. Reuse that hand choice and leave all fan traces,
			 * combo state and damage in its original QuakeC callback. */
			const int hand = ent->v.weaponframe == 14 || ent->v.weaponframe == 64;
			vec3_t temporary_origin;
			VectorCopy (scope->akimbo_muzzle[hand], muzzle);
			VectorCopy (ent->v.origin, temporary_origin);
			VectorCopy (scope->body_origin, ent->v.origin);
			SV_ClampVRMuzzleToWorld (ent, muzzle);
			VectorCopy (temporary_origin, ent->v.origin);
			VectorCopy (scope->akimbo_angles[hand], ent->v.v_angle);
			ent->v.v_angle[ROLL] = 0;
			AngleVectors (ent->v.v_angle, pr_global_struct->v_forward,
				pr_global_struct->v_right, pr_global_struct->v_up);
			VectorSubtract (muzzle, ent->v.view_ofs, ent->v.origin);
			return;
		}
	}

	VectorAdd (scope->origin, cmd->vr_handpos, muzzle);
	VectorCopy (cmd->vr_handrot, ent->v.v_angle);
	/* Retain raw roll only for the exact pinned QBJ3 pistol/Flak QC. The
	 * temporary entity angle below remains camera-safe for ordinary QC. */
	if (SV_QBJ3TwinNailgunProgramLoaded () &&
		(ent->v.weapon == IT_SHOTGUN || ent->v.weapon == IT_SUPER_SHOTGUN) &&
		isfinite (ent->v.v_angle[ROLL]))
	{
		scope->qbj3_shotgun_spread = true;
		scope->qbj3_shotgun_weapon = (int)ent->v.weapon;
		scope->qbj3_shotgun_roll = ent->v.v_angle[ROLL];
		if (ent->v.weapon == IT_SUPER_SHOTGUN &&
			isfinite (ent->v.v_angle[PITCH]) &&
			isfinite (ent->v.v_angle[YAW]))
		{
			qbj3_flak_source = true;
			VectorCopy (ent->v.v_angle, flak_source_angles);
		}
	}
	/* QC roll is camera tilt, while wrist roll belongs to the weapon model. */
	ent->v.v_angle[ROLL] = 0;
	AngleVectors (ent->v.v_angle, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
	SV_ClampVRMuzzleToWorld (ent, muzzle);
	/* A nested same-player scope starts from its parent's temporary source.
	 * Do not cache that as a new authoritative body-relative muzzle. */
	if (!previous)
	{
		VectorCopy (muzzle, scope->stock_id1_muzzle);
		scope->stock_id1_muzzle_valid = true;
	}
	VR_WeaponCalibrationProjectileSourceOffset (
		PR_GetString (ent->v.weaponmodel), (int)ent->v.weapon,
		qbj3_flak_source ? flak_source_angles :
			ent->v.v_angle, ent->v.view_ofs[2], source_offset);
	VectorSubtract (muzzle, source_offset, ent->v.origin);
}

qboolean SV_QBJ3AkimboAim (edict_t *ent, vec3_t muzzle)
{
	sv_vr_weapon_pose_scope_t *scope;
	vec3_t temporary_origin, source;
	int hand, axis;
	float side_offset;

	if (!ent || !qcvm || qcvm != &sv.qcvm || !qcvm->xfunction ||
		!SV_QBJ3TwinNailgunProgramLoaded () ||
		strcmp (PR_GetString (qcvm->xfunction->s_name), "W_FireTwinNailgun") ||
		strcmp (PR_GetString (ent->v.weaponmodel), "progs/v_tnailgun.mdl") ||
		ent->v.weapon != 4 ||
		(ent->v.weaponframe != 11 && ent->v.weaponframe != 15))
		return false;

	for (scope = sv_vr_weapon_pose_scope; scope; scope = scope->previous)
		if (scope->ent == ent)
			break;
	if (!scope || !scope->applied || !scope->akimbo_pose_valid ||
		scope->akimbo_invalidated)
		return false;

	/* QBJ3 frame 11 fires from the anatomical right hand; frame 15 from left. */
	hand = ent->v.weaponframe == 11 ? 1 : 0;
	side_offset = hand ? 4.0f : -4.0f;
	VectorCopy (scope->akimbo_muzzle[hand], muzzle);
	VectorCopy (ent->v.origin, temporary_origin);
	VectorCopy (scope->body_origin, ent->v.origin);
	SV_ClampVRMuzzleToWorld (ent, muzzle);
	VectorCopy (temporary_origin, ent->v.origin);

	VectorCopy (scope->akimbo_angles[hand], ent->v.v_angle);
	ent->v.v_angle[ROLL] = 0;
	AngleVectors (ent->v.v_angle, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
	for (axis = 0; axis < 3; axis++)
		source[axis] = ent->v.view_ofs[axis] +
			11.0f * pr_global_struct->v_forward[axis] +
			side_offset * pr_global_struct->v_right[axis] -
			6.0f * pr_global_struct->v_up[axis];
	VectorSubtract (muzzle, source, ent->v.origin);
	return true;
}

static sv_vr_weapon_pose_scope_t *SV_FindPrivateVRWeaponPose (edict_t *ent)
{
	sv_vr_weapon_pose_scope_t *scope;
	for (scope = sv_vr_weapon_pose_scope; scope; scope = scope->previous)
		if (scope->ent == ent)
			return scope; /* The first match masks any older nested pose. */
	return NULL;
}

/* Apply the native paired pose only at Dwell's source-pinned native strike
 * site. QC retains its authored range, trace and damage behavior. */
qboolean SV_DwellBerserkAkimboMakevectors (void)
{
	sv_vr_weapon_pose_scope_t *scope;
	edict_t *ent;
	int hand;
	vec3_t muzzle, temporary_origin, source;

	if (!sv_vr_weapon_pose_scope)
		return false;
	ent = SV_EnyoAkimboSelf ();
	if (!ent || ent->free)
		return false;
	scope = SV_FindPrivateVRWeaponPose (ent);
	if (!scope || !scope->applied || scope->ent != ent ||
		!scope->dwell_berserk_pose_valid || scope->origin_relocated ||
		scope->akimbo_invalidated)
		return false;
	if (!SV_DwellBerserkAkimboWeaponSelected (ent) || !qcvm->xfunction ||
		qcvm->xfunction != &qcvm->functions[DWELL_W_FIREAXE_FUNCTION] ||
		qcvm->xstatement != DWELL_W_FIREAXE_MAKEVECTORS_STATEMENT ||
		!SV_DwellBerserkStrikeHand (ent->v.weaponframe, &hand) ||
		!scope->akimbo_pose_valid ||
		!SV_EnyoVectorIsFinite (scope->body_origin) ||
		!SV_EnyoVectorIsFinite (ent->v.view_ofs) ||
		!SV_EnyoVectorIsFinite (scope->akimbo_muzzle[0]) ||
		!SV_EnyoVectorIsFinite (scope->akimbo_muzzle[1]) ||
		!SV_EnyoVectorIsFinite (scope->akimbo_angles[0]) ||
		!SV_EnyoVectorIsFinite (scope->akimbo_angles[1]))
		return false;

	VectorCopy (scope->akimbo_muzzle[hand], muzzle);
	VectorCopy (ent->v.origin, temporary_origin);
	VectorCopy (scope->body_origin, ent->v.origin);
	SV_ClampVRMuzzleToWorld (ent, muzzle);
	VectorCopy (temporary_origin, ent->v.origin);
	if (!SV_EnyoVectorIsFinite (muzzle))
		return false;
	VectorSubtract (muzzle, ent->v.view_ofs, source);
	if (!SV_EnyoVectorIsFinite (source))
		return false;

	VectorCopy (scope->akimbo_angles[hand], ent->v.v_angle);
	ent->v.v_angle[ROLL] = 0;
	AngleVectors (ent->v.v_angle, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
	VectorCopy (source, ent->v.origin);
	return true;
}

/* QBJ3 labels IT_SHOTGUN as its pistol and IT_SUPER_SHOTGUN as Flak.
 * Their audited FireBullets/Flak calls use makevectors for spread/source
 * basis, so restore only the applied private controller's physical wrist roll. */
qboolean SV_QBJ3ShotgunSpreadBasis (const vec3_t angles)
{
	const char *function_name;
	sv_vr_weapon_pose_scope_t *scope;
	edict_t *ent;
	vec3_t spread_angles, forward, right, up;
	int self;

	if (!angles || !qcvm || qcvm != &sv.qcvm ||
		!SV_QBJ3TwinNailgunProgramLoaded () || !pr_global_struct ||
		!qcvm->xfunction || !qcvm->edicts || qcvm->edict_size <= 0)
		return false;

	function_name = PR_GetString (qcvm->xfunction->s_name);
	if (strcmp (function_name, "FireBullets") &&
		strcmp (function_name, "W_FireFlakShotgun"))
		return false;

	self = pr_global_struct->self;
	if (self < 0 || self % qcvm->edict_size ||
		self / qcvm->edict_size >= qcvm->num_edicts)
		return false;
	ent = PROG_TO_EDICT (self);
	if (!ent || ent->free ||
		(ent->v.weapon != IT_SHOTGUN && ent->v.weapon != IT_SUPER_SHOTGUN))
		return false;

	scope = SV_FindPrivateVRWeaponPose (ent);
	if (!scope || !scope->applied || scope->origin_relocated ||
		scope->ent != ent ||
		!scope->qbj3_shotgun_spread ||
		scope->qbj3_shotgun_weapon != (int)ent->v.weapon ||
		!isfinite (scope->qbj3_shotgun_roll) ||
		!isfinite (angles[0]) || !isfinite (angles[1]) ||
		!isfinite (angles[2]))
		return false;

	VectorCopy (angles, spread_angles);
	spread_angles[ROLL] = scope->qbj3_shotgun_roll;
	AngleVectors (spread_angles, forward, right, up);
	/* PF_makevectors has already computed the ordinary forward. */
	VectorCopy (right, pr_global_struct->v_right);
	VectorCopy (up, pr_global_struct->v_up);
	return true;
}

qboolean SV_EnyoAkimboMakevectors (void)
{
	sv_vr_weapon_pose_scope_t *scope;
	dfunction_t *function;
	edict_t *ent;
	int hand;
	float offs, t0;
	vec3_t muzzle, forward, right, up, angles, origin;
	vec3_t source_offset, clearance_start, temporary_origin;
	trace_t reverse;

	ent = SV_EnyoAkimboSelf ();
	function = qcvm ? qcvm->xfunction : NULL;
	if (!ent || ent->free || !SV_EnyoSMGWeapon (ent) ||
		!function || !SV_EnyoSMGFunction (function) ||
		qcvm->xstatement != ENYO_W_FIRESMG_MAKEVECTORS_STATEMENT)
		return false;
	scope = SV_FindPrivateVRWeaponPose (ent);
	if (!scope || !scope->applied || !scope->akimbo_pose_valid ||
		scope->akimbo_invalidated)
		return false;

	/* A new audited call supersedes any unconsumed clearance in this scope. */
	scope->enyo_clearance_pending = false;
	if (!SV_EnyoVectorIsFinite (scope->body_origin) ||
		!SV_EnyoVectorIsFinite (ent->v.view_ofs) ||
		!SV_EnyoVectorIsFinite (scope->akimbo_muzzle[0]) ||
		!SV_EnyoVectorIsFinite (scope->akimbo_muzzle[1]) ||
		!SV_EnyoVectorIsFinite (scope->akimbo_angles[0]) ||
		!SV_EnyoVectorIsFinite (scope->akimbo_angles[1]))
		return false;

	/* offs lives in W_FireSMG's local frame; OFS_PARM0 now holds angle. */
	offs = qcvm->globals[function->parm_start];
	if (offs != 0.0f && offs != 1.0f)
		return false;
	hand = offs == 0.0f ? 1 : 0;
	VectorCopy (scope->akimbo_angles[hand], angles);
	angles[ROLL] = 0;
	AngleVectors (angles, forward, right, up);
	VectorCopy (scope->akimbo_muzzle[hand], muzzle);

	/* Clamp from the saved body eye, then restore the scope's temporary origin. */
	VectorCopy (ent->v.origin, temporary_origin);
	VectorCopy (scope->body_origin, ent->v.origin);
	SV_ClampVRMuzzleToWorld (ent, muzzle);
	VectorCopy (temporary_origin, ent->v.origin);
	if (!SV_EnyoVectorIsFinite (muzzle))
		return false;

	/* Move QC's original 16-unit start forward if it begins in a brush. */
	VectorMA (muzzle, -16.0f, forward, clearance_start);
	reverse = SV_Move (muzzle, vec3_origin, vec3_origin, clearance_start,
		MOVE_NOMONSTERS, ent);
	if (reverse.startsolid || reverse.allsolid)
		return false;
	VectorCopy (clearance_start, scope->enyo_clearance_adjusted_start);
	if (reverse.fraction < 1.0f)
	{
		VectorCopy (reverse.endpos, scope->enyo_clearance_adjusted_start);
		VectorMA (scope->enyo_clearance_adjusted_start, 1.0f, forward,
			scope->enyo_clearance_adjusted_start);
	}
	VectorSubtract (scope->enyo_clearance_adjusted_start, clearance_start,
		source_offset);
	t0 = DotProduct (source_offset, forward) / 16.0f;
	if (!isfinite (t0) || t0 < 0.0f)
		return false;
	if (t0 >= 1.0f)
	{
		VectorCopy (muzzle, scope->enyo_clearance_adjusted_start);
		t0 = 1.0f;
	}

	/* Reconstruct QC's untouched origin + view_ofs - 6up +/- 7right = B. */
	VectorCopy (ent->v.view_ofs, source_offset);
	VectorMA (source_offset, -6.0f, up, source_offset);
	VectorMA (source_offset, hand ? 7.0f : -7.0f, right, source_offset);
	VectorSubtract (clearance_start, source_offset, origin);
	if (!SV_EnyoVectorIsFinite (origin))
		return false;

	VectorCopy (origin, ent->v.origin);
	VectorCopy (angles, ent->v.v_angle);
	VectorCopy (forward, pr_global_struct->v_forward);
	VectorCopy (right, pr_global_struct->v_right);
	VectorCopy (up, pr_global_struct->v_up);
	scope->enyo_clearance_pending = true;
	VectorCopy (clearance_start, scope->enyo_clearance_start);
	VectorCopy (muzzle, scope->enyo_clearance_end);
	scope->enyo_clearance_t0 = t0;
	return true;
}

qboolean SV_EnyoAkimboAim (edict_t *ent, vec3_t muzzle)
{
	sv_vr_weapon_pose_scope_t *scope;
	if (!ent || ent != SV_EnyoAkimboSelf () || !SV_EnyoSMGWeapon (ent) ||
		!qcvm->xfunction || !SV_EnyoSMGFunction (qcvm->xfunction) ||
		qcvm->xstatement != ENYO_W_FIRESMG_AIM_STATEMENT)
		return false;
	scope = SV_FindPrivateVRWeaponPose (ent);
	if (!scope || !scope->applied || !scope->akimbo_pose_valid ||
		scope->akimbo_invalidated || !scope->enyo_clearance_pending ||
		!SV_EnyoVectorIsFinite (scope->enyo_clearance_end))
		return false;
	VectorCopy (scope->enyo_clearance_end, muzzle);
	return true;
}

qboolean SV_EnyoAkimboTrace (edict_t *ent, const vec3_t start,
	const vec3_t end, int nomonsters, trace_t *trace)
{
	sv_vr_weapon_pose_scope_t *scope;
	if (!trace || !ent || ent != SV_EnyoAkimboSelf () ||
		!SV_EnyoSMGWeapon (ent) || !qcvm->xfunction ||
		!SV_EnyoSMGFunction (qcvm->xfunction) ||
		qcvm->xstatement != ENYO_W_FIRESMG_TRACELINE_STATEMENT ||
		pr_global_struct->self != EDICT_TO_PROG (ent) || nomonsters != 0 ||
		!SV_EnyoVectorIsFinite (start) || !SV_EnyoVectorIsFinite (end))
		return false;
	scope = SV_FindPrivateVRWeaponPose (ent);
	if (!scope || !scope->applied || !scope->akimbo_pose_valid ||
		scope->akimbo_invalidated || !scope->enyo_clearance_pending ||
		!SV_EnyoVectorIsFinite (scope->enyo_clearance_start) ||
		!SV_EnyoVectorIsFinite (scope->enyo_clearance_end) ||
		!SV_EnyoVectorIsFinite (scope->enyo_clearance_adjusted_start) ||
		!SV_EnyoVectorsNear (start, scope->enyo_clearance_start) ||
		!SV_EnyoVectorsNear (end, scope->enyo_clearance_end))
		return false;

	/* Only this exact QC trace consumes the one-shot. Keep fraction in B..M. */
	scope->enyo_clearance_pending = false;
	*trace = SV_Move (scope->enyo_clearance_adjusted_start, vec3_origin,
		vec3_origin, scope->enyo_clearance_end, nomonsters, ent);
	trace->fraction = scope->enyo_clearance_t0 +
		(1.0f - scope->enyo_clearance_t0) * trace->fraction;
	return true;
}

static void SV_EndPrivateVRWeaponPose (edict_t *ent,
	sv_vr_weapon_pose_scope_t *scope)
{
	scope->enyo_clearance_pending = false;
	sv_vr_weapon_pose_scope = scope->previous;
	if (!scope->applied)
		return;
	if (!ent->free)
	{
		if (!scope->origin_relocated)
		{
			VectorCopy (scope->origin, ent->v.origin);
			/* A QC size/model/link operation may have indexed the hand origin. */
			if (scope->linked)
				SV_LinkEdict (ent, false);
		}
		VectorCopy (scope->v_angle, ent->v.v_angle);
	}
	VectorCopy (scope->forward, pr_global_struct->v_forward);
	VectorCopy (scope->right, pr_global_struct->v_right);
	VectorCopy (scope->up, pr_global_struct->v_up);
}

#define SV_VR_CONTACT_MAX_OFFSET 96.0f
#define SV_VR_CONTACT_MAX_SPEED 20.0f
#define SV_VR_CONTACT_MAX_FRESHNESS 0.25
#define SV_VR_CONTACT_MAX_PLAYER_AGE 1.0

static void SV_ResetPrivateVRDirectMeleeStroke (client_t *client, int hand)
{
	client->private_vr_direct_melee_authorized[hand] = false;
	client->private_vr_direct_melee_subtype[hand] = SV_VR_DIRECT_MELEE_NONE;
	client->private_vr_direct_melee_deadline[hand] = 0;
	client->private_vr_direct_melee_hit_count[hand] = 0;
	memset (client->private_vr_direct_melee_hit_entities[hand], 0,
		sizeof (client->private_vr_direct_melee_hit_entities[hand]));
}

static void SV_ResetPrivateVRContactContinuity (client_t *client)
{
	client->private_vr_contact_previous_valid = false;
	memset (&client->private_vr_contact_previous, 0,
		sizeof (client->private_vr_contact_previous));
	VectorClear (client->private_vr_contact_body_origin);
	client->private_vr_contact_previous_received = 0;
	memset (client->private_vr_contact_button, 0,
		sizeof (client->private_vr_contact_button));
	memset (client->private_vr_melee_arc, 0,
		sizeof (client->private_vr_melee_arc));
	memset (client->private_vr_melee_peak_speed, 0,
		sizeof (client->private_vr_melee_peak_speed));
	memset (client->private_vr_melee_consumed, 0,
		sizeof (client->private_vr_melee_consumed));
	memset (client->private_vr_melee_stroke_direction, 0,
		sizeof (client->private_vr_melee_stroke_direction));
	memset (client->private_vr_melee_stroke_endpoint, 0,
		sizeof (client->private_vr_melee_stroke_endpoint));
	for (int hand = 0; hand < 2; hand++)
		SV_ResetPrivateVRDirectMeleeStroke (client, hand);
}

/* Profile availability is tied to the complete pinned id1 handler below.
 * Server policy controls whether that profile is offered to private peers. */
unsigned int SV_VRStockAxeContactProfile (void)
{
	const sv_vr_stock_axe_descriptor_t *descriptor =
		SV_VRStockAxeMeleeDescriptor ();
	return !descriptor ? VR_WEAPON_CONTACT_PROFILE_NONE :
		descriptor->alkaline ? VR_WEAPON_CONTACT_PROFILE_ALK :
		VR_WEAPON_CONTACT_PROFILE_STOCK;
}

/* Stock id1 FireBullets overwrites src.z with absmin.z + 0.7 * size.z.
 * Translating self.origin alone therefore cannot move its pellet rays to the
 * tracked muzzle. Borrow only the exact pinned player's FireBullets trace;
 * QuakeC still owns spread, pellet count, damage and weapon timing. */
qboolean SV_VRStockShotgunTrace (edict_t *ignore, int nomonsters,
	const vec3_t start, const vec3_t end, trace_t *trace)
{
	sv_vr_weapon_pose_scope_t *scope;
	vec3_t expected, translated_end, delta;
	int axis;

	if (!trace || !ignore || ignore->free || nomonsters ||
		qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->progs->numfunctions <= 192 ||
		qcvm->xfunction != &qcvm->functions[192] ||
		qcvm->xstatement != 3646 ||
		!pr_global_struct || pr_global_struct->self != EDICT_TO_PROG (ignore) ||
		(ignore->v.weapon != IT_SHOTGUN &&
		 ignore->v.weapon != IT_SUPER_SHOTGUN))
		return false;
	scope = SV_FindPrivateVRWeaponPose (ignore);
	if (!scope || !scope->applied || !scope->stock_id1_muzzle_valid ||
		scope->origin_relocated || !scope->client ||
		!scope->client->active || !scope->client->spawned ||
		scope->client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		scope->client->edict != ignore)
		return false;
	if (!SV_VRStockID1Program (scope))
		return false;

	/* Check the actual QC start before shifting a ray. This leaves an altered
	 * callback or unexpected source expression on its unmodified path. */
	VectorMA (ignore->v.origin, 10.0f, pr_global_struct->v_forward, expected);
	expected[2] = ignore->v.absmin[2] + ignore->v.size[2] * 0.7f;
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (start[axis]) || !isfinite (end[axis]) ||
			!isfinite (scope->stock_id1_muzzle[axis]) ||
			!isfinite (expected[axis]) ||
			fabsf (start[axis] - expected[axis]) > 0.125f)
			return false;
	VectorSubtract (end, start, delta);
	VectorAdd (scope->stock_id1_muzzle, delta, translated_end);
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (translated_end[axis]))
			return false;
	*trace = SV_Move (scope->stock_id1_muzzle, vec3_origin, vec3_origin,
		translated_end, nomonsters, ignore);
	return true;
}

/* The pinned stock id1 launch_spike callback passes self.origin + 16 up,
 * optionally plus four units of QC v_right, to setorigin. Generic weapon
 * source compensation leaves its nails eight units behind a tracked muzzle.
 * Move only this projectile at the existing spawn/link boundary; QuakeC
 * retains its ammo, alternating barrels, velocity, damage and effects. */
qboolean SV_VRStockNailSetOrigin (edict_t *projectile, const vec3_t authored,
	vec3_t translated)
{
	sv_vr_weapon_pose_scope_t *scope;
	edict_t *player;
	vec3_t expected, lateral, barrel, candidate, difference;
	trace_t clearance;
	float safe_fraction;
	int axis;

	if (!projectile || projectile->free || qcvm != &sv.qcvm ||
		!qcvm->progs || qcvm->progs->numfunctions <= 209 ||
		qcvm->xfunction != &qcvm->functions[209] ||
		qcvm->xstatement != 4169)
		return false;
	player = SV_EnyoAkimboSelf ();
	if (!player || player->free || player == projectile ||
		projectile->v.owner != EDICT_TO_PROG (player) ||
		(player->v.weapon != IT_NAILGUN &&
		 player->v.weapon != IT_SUPER_NAILGUN))
		return false;
	scope = SV_FindPrivateVRWeaponPose (player);
	if (!scope || !scope->applied || !scope->stock_id1_muzzle_valid ||
		scope->origin_relocated || !scope->client ||
		!scope->client->active || !scope->client->spawned ||
		scope->client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		scope->client->edict != player)
		return false;
	if (!SV_VRStockID1Program (scope))
		return false;

	VectorCopy (player->v.origin, expected);
	expected[2] += 16.0f;
	VectorSubtract (authored, expected, lateral);
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (authored[axis]) ||
			!isfinite (scope->stock_id1_muzzle[axis]) ||
			!isfinite (pr_global_struct->v_right[axis]) ||
			!isfinite (lateral[axis]))
			return false;
	VectorScale (pr_global_struct->v_right, 4.0f, barrel);
	VectorSubtract (lateral, barrel, difference);
	if (!SV_EnyoVectorsNear (lateral, vec3_origin) &&
		!SV_EnyoVectorsNear (difference, vec3_origin))
	{
		VectorAdd (lateral, barrel, difference);
		if (!SV_EnyoVectorsNear (difference, vec3_origin))
			return false;
	}
	VectorAdd (scope->stock_id1_muzzle, lateral, candidate);
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (candidate[axis]))
			return false;
	/* The center was clamped against the world at scope entry. Sweep the
	 * displaced barrel too; otherwise a nearby corner can embed a nail.
	 * Even a centered spike needs a start-solid check: the earlier clamp can
	 * fall back to an eye position that is itself inside solid geometry. */
	clearance = SV_Move (scope->stock_id1_muzzle, vec3_origin, vec3_origin,
		candidate, MOVE_NOMONSTERS, player);
	if (clearance.startsolid || clearance.allsolid)
		return false;
	if (!isfinite (clearance.fraction) || clearance.fraction < 0.0f ||
		clearance.fraction > 1.0f)
		return false;
	if (SV_EnyoVectorsNear (lateral, vec3_origin))
	{
		VectorCopy (scope->stock_id1_muzzle, translated);
		return true;
	}
	/* The verified barrel offset is at most four units. Back off up to one
	 * unit along that short sweep when it meets the world. */
	safe_fraction = clearance.fraction < 1.0f ?
		q_max (0.0f, clearance.fraction - 0.25f) : 1.0f;
	VectorMA (scope->stock_id1_muzzle, safe_fraction, lateral, translated);
	return true;
}

static sv_vr_weapon_pose_scope_t *SV_VRStockLightningScope (void)
{
	sv_vr_weapon_pose_scope_t *scope;
	edict_t *player = SV_EnyoAkimboSelf ();

	if (!player || player->free || player->v.weapon != IT_LIGHTNING ||
		!qcvm->progs || qcvm->progs->numfunctions <= 205)
		return NULL;
	scope = SV_FindPrivateVRWeaponPose (player);
	if (!scope || !scope->applied || !scope->stock_id1_muzzle_valid ||
		scope->origin_relocated || !scope->client ||
		!scope->client->active || !scope->client->spawned ||
		scope->client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		scope->client->edict != player ||
		!SV_VRStockID1Program (scope))
		return NULL;
	return scope;
}

/* Stock W_FireLightning first traces from self.origin + 16 up. The generic
 * temporary QC source is eight forward and 16 up behind the physical muzzle;
 * borrow only this exact world-only trace so the beam endpoint follows the
 * tracked aim. The beam's three start coordinates are patched separately at
 * their WriteCoord sites, leaving all QC globals and the message owner intact. */
qboolean SV_VRStockLightningBeamTrace (edict_t *ignore, int nomonsters,
	const vec3_t start, const vec3_t end, trace_t *trace)
{
	sv_vr_weapon_pose_scope_t *scope;
	vec3_t expected_start, expected_end, delta, translated_end;
	int axis;

	if (!trace || !nomonsters || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->progs->numfunctions <= 205 ||
		qcvm->xfunction != &qcvm->functions[205] ||
		qcvm->xstatement != 3983 ||
		!(scope = SV_VRStockLightningScope ()) || ignore != scope->ent)
		return false;
	/* A second shot in the same QC scope must earn its own beam/trace pair. */
	scope->stock_lightning_trace_applied = false;
	scope->stock_lightning_damage_started = false;
	VectorCopy (ignore->v.origin, expected_start);
	expected_start[2] += 16.0f;
	VectorMA (expected_start, 600.0f, pr_global_struct->v_forward,
		expected_end);
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (start[axis]) || !isfinite (end[axis]) ||
			!isfinite (expected_start[axis]) ||
			!isfinite (expected_end[axis]) ||
			!isfinite (scope->stock_id1_muzzle[axis]))
			return false;
	if (!SV_EnyoVectorsNear (start, expected_start) ||
		!SV_EnyoVectorsNear (end, expected_end))
		return false;
	VectorSubtract (end, start, delta);
	VectorAdd (scope->stock_id1_muzzle, delta, translated_end);
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (translated_end[axis]))
			return false;
	*trace = SV_Move (scope->stock_id1_muzzle, vec3_origin, vec3_origin,
		translated_end, nomonsters, ignore);
	if (trace->startsolid || trace->allsolid)
		return false;
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (trace->endpos[axis]))
			return false;
	VectorCopy (trace->endpos, scope->stock_lightning_end);
	scope->stock_lightning_trace_applied = true;
	return true;
}

qboolean SV_VRStockLightningBeamCoord (float authored, float *translated)
{
	sv_vr_weapon_pose_scope_t *scope;
	int axis;
	float expected;

	if (!translated || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->progs->numfunctions <= 205 ||
		qcvm->xfunction != &qcvm->functions[205] ||
		!(scope = SV_VRStockLightningScope ()) ||
		!scope->stock_lightning_trace_applied ||
		pr_global_struct->self != EDICT_TO_PROG (scope->ent) ||
		(int)G_FLOAT (OFS_PARM0) != MSG_BROADCAST)
		return false;
	switch (qcvm->xstatement)
	{
	case 3995: axis = 0; break;
	case 3998: axis = 1; break;
	case 4001: axis = 2; break;
	default: return false;
	}
	expected = scope->ent->v.origin[axis] + (axis == 2 ? 16.0f : 0.0f);
	if (!isfinite (authored) || !isfinite (expected) ||
		!isfinite (scope->stock_id1_muzzle[axis]) ||
		fabsf (authored - expected) > 0.125f)
		return false;
	*translated = scope->stock_id1_muzzle[axis];
	return true;
}

/* Preserve stock LightningDamage's three authored side offsets, including
 * its unusual unnormalized QC arithmetic. Move only their starts to the
 * physical muzzle; QC still determines trace endpoints, target uniqueness,
 * particles and damage. A blocked translated start is an admitted miss,
 * not an invitation to damage from the old source behind the beam. */
qboolean SV_VRStockLightningDamageTrace (edict_t *ignore, int nomonsters,
	const vec3_t start, const vec3_t end, trace_t *trace)
{
	sv_vr_weapon_pose_scope_t *scope;
	vec3_t side, expected_end, translated_start, target;
	trace_t clearance;
	int axis;

	if (!trace || nomonsters || qcvm != &sv.qcvm || !qcvm->progs ||
		qcvm->progs->numfunctions <= 204 ||
		qcvm->xfunction != &qcvm->functions[204] ||
		(qcvm->xstatement != 3860 && qcvm->xstatement != 3891 &&
		 qcvm->xstatement != 3914) ||
		!(scope = SV_VRStockLightningScope ()) ||
		!scope->stock_lightning_trace_applied || ignore != scope->ent)
		return false;
	if (qcvm->xstatement != 3860 && !scope->stock_lightning_damage_started)
		return false;
	VectorSubtract (start, ignore->v.origin, side);
	if (qcvm->xstatement == 3860)
		VectorMA (scope->stock_lightning_end, 4.0f,
			pr_global_struct->v_forward, expected_end);
	else
		VectorCopy (scope->stock_lightning_damage_end, expected_end);
	VectorAdd (expected_end, side, expected_end);
	VectorAdd (scope->stock_id1_muzzle, side, translated_start);
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (start[axis]) || !isfinite (end[axis]) ||
			!isfinite (side[axis]) || !isfinite (expected_end[axis]) ||
			!isfinite (translated_start[axis]))
			return false;
	if (!SV_EnyoVectorsNear (end, expected_end) ||
		(qcvm->xstatement == 3860 &&
		 !SV_EnyoVectorsNear (side, vec3_origin)) ||
		(qcvm->xstatement != 3860 &&
		 (fabsf (side[2]) > 0.125f ||
		  fabsf (side[0] - side[1]) > 0.125f)))
		return false;
	clearance = SV_Move (translated_start, vec3_origin, vec3_origin,
		translated_start, MOVE_NOMONSTERS, ignore);
	if (clearance.startsolid || clearance.allsolid)
	{
		*trace = clearance;
		trace->ent = qcvm->edicts; /* world cannot take LightningDamage */
		trace->fraction = 0.0f;
		VectorCopy (translated_start, trace->endpos);
	}
	else
	{
		VectorCopy (end, target);
		*trace = SV_Move (translated_start, vec3_origin, vec3_origin,
			target, nomonsters, ignore);
	}
	if (qcvm->xstatement == 3860)
	{
		VectorCopy (end, scope->stock_lightning_damage_end);
		scope->stock_lightning_damage_started = true;
	}
	return true;
}

int SV_VRStockAxeTraceStatement (void)
{
	const sv_vr_stock_axe_descriptor_t *descriptor = SV_VRStockAxeMeleeDescriptor ();
	return descriptor ? descriptor->trace_statement : -1;
}

void SV_VRStockAxeClearTraceScope (void)
{
	memset (&sv_vr_axe_trace_scope, 0, sizeof (sv_vr_axe_trace_scope));
}

qboolean SV_VRStockAxeTrace (edict_t *ignore, int nomonsters,
	const vec3_t start, const vec3_t end, trace_t *trace)
{
	const sv_vr_stock_axe_descriptor_t *descriptor;
	int axis;

	if (!trace || !sv_vr_axe_trace_scope.active ||
		sv_vr_axe_trace_scope.mode != SV_VR_AXE_TRACE_SCOPE_STOCK ||
		qcvm != &sv.qcvm || !qcvm->progs ||
		!(descriptor = SV_VRStockAxeMeleeDescriptor ()) ||
		!sv_vr_axe_trace_scope.client ||
		!sv_vr_axe_trace_scope.client->active ||
		!sv_vr_axe_trace_scope.client->spawned ||
		sv_vr_axe_trace_scope.client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		sv_vr_axe_trace_scope.client->edict !=
			sv_vr_axe_trace_scope.player ||
		!sv_vr_axe_trace_scope.player ||
		sv_vr_axe_trace_scope.player->free ||
		ignore != sv_vr_axe_trace_scope.player || nomonsters ||
		qcvm->xfunction != sv_vr_axe_trace_scope.function ||
		qcvm->xfunction != &qcvm->functions[descriptor->leaf_index] ||
		qcvm->xstatement != descriptor->trace_statement ||
		pr_global_struct->self != EDICT_TO_PROG (sv_vr_axe_trace_scope.player))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (start[axis]) || !isfinite (end[axis]))
			return false;
	*trace = sv_vr_axe_trace_scope.trace;
	SV_VRStockAxeClearTraceScope ();
	return true;
}

/* Dwell's berserk-only W_FireAxe call enters traceline2 once for physical
 * acquisition. Its helper retries must see a clean world miss so its own
 * filtering cannot turn the same physical contact into another hit. This
 * scope is armed by the queued contact outcome owner; admission is not part
 * of this adapter. */
qboolean SV_VRDwellBerserkTrace (edict_t *ignore, int nomonsters,
	const vec3_t start, const vec3_t end, trace_t *trace)
{
	prstack_t *caller;
	qboolean finite_end = true;
	int axis;

	if (!trace || !sv_vr_axe_trace_scope.active ||
		sv_vr_axe_trace_scope.mode != SV_VR_AXE_TRACE_SCOPE_DWELL ||
		qcvm != &sv.qcvm || !qcvm->progs || !pr_global_struct ||
		qcvm->progs->numfunctions <= DWELL_W_FIREAXE_FUNCTION ||
		qcvm->progs->numfunctions <= DWELL_TRACELINE2_FUNCTION ||
		qcvm->depth <= 0)
		return false;
	caller = &qcvm->stack[qcvm->depth - 1];
	/* Leave unrelated traceline calls entirely native. Once this exact nested
	 * bytecode site owns the call, every failed admission check below becomes a
	 * miss rather than falling through to SV_Move. */
	if (sv_vr_axe_trace_scope.function !=
			&qcvm->functions[DWELL_W_FIREAXE_FUNCTION] ||
		qcvm->xfunction != &qcvm->functions[DWELL_TRACELINE2_FUNCTION] ||
		qcvm->xstatement != 12923 || caller->f !=
			sv_vr_axe_trace_scope.function ||
		(caller->s != 14578 && caller->s != 14586))
		return false;
	/* The sibling is W_FireAxe's ordinary trace branch. A rounded expiry
	 * or callback change must never let it acquire a native target. */
	if (caller->s == 14586)
		sv_vr_axe_trace_scope.dwell_force_miss = true;

	if (!SV_DwellBerserkAkimboProgramLoaded () ||
		!sv_vr_axe_trace_scope.player ||
		sv_vr_axe_trace_scope.player->free ||
		!SV_DwellBerserkAkimboWeaponSelected (sv_vr_axe_trace_scope.player) ||
		pr_global_struct->self !=
			EDICT_TO_PROG (sv_vr_axe_trace_scope.player) || nomonsters)
		sv_vr_axe_trace_scope.dwell_invalidated = true;
	for (axis = 0; axis < 3; axis++)
	{
		if (!isfinite (start[axis]) || !isfinite (end[axis]))
			sv_vr_axe_trace_scope.dwell_invalidated = true;
		if (!isfinite (end[axis]))
			finite_end = false;
	}

	if (!sv_vr_axe_trace_scope.dwell_force_miss &&
		!sv_vr_axe_trace_scope.dwell_invalidated)
	{
		if (ignore != sv_vr_axe_trace_scope.player ||
			!sv_vr_axe_trace_scope.dwell_has_contact ||
			!sv_vr_axe_trace_scope.trace.ent ||
			sv_vr_axe_trace_scope.trace.ent->free ||
			!isfinite (sv_vr_axe_trace_scope.trace.fraction) ||
			sv_vr_axe_trace_scope.trace.fraction < 0 ||
			sv_vr_axe_trace_scope.trace.fraction >= 1 ||
			sv_vr_axe_trace_scope.trace.startsolid ||
			sv_vr_axe_trace_scope.trace.allsolid ||
			!isfinite (sv_vr_axe_trace_scope.trace.endpos[0]) ||
			!isfinite (sv_vr_axe_trace_scope.trace.endpos[1]) ||
			!isfinite (sv_vr_axe_trace_scope.trace.endpos[2]))
			sv_vr_axe_trace_scope.dwell_invalidated = true;
	}
	if (sv_vr_axe_trace_scope.dwell_invalidated)
		sv_vr_axe_trace_scope.dwell_force_miss = true;

	if (!sv_vr_axe_trace_scope.dwell_force_miss &&
		!sv_vr_axe_trace_scope.dwell_invalidated)
	{
		*trace = sv_vr_axe_trace_scope.trace;
		sv_vr_axe_trace_scope.dwell_force_miss = true;
		return true;
	}

	/* A whiff starts in this phase too. Use the same complete miss shape as
	 * Quake's regular world trace so traceline2 can safely consume its globals. */
	memset (trace, 0, sizeof (*trace));
	trace->fraction = 1;
	trace->inopen = true;
	trace->ent = qcvm->edicts;
	if (finite_end)
		VectorCopy (end, trace->endpos);
	return true;
}

/* Called while the returning function's locals and QC trace globals are still
 * live. Restore only the accepted spatial fraction after traceline2's retry
 * filtering, and retire the scope when its W_FireAxe root returns. */
void SV_VRAxeTraceLeaveFunction (void)
{
	prstack_t *caller;
	const trace_t *contact;
	int axis;

	if (!sv_vr_axe_trace_scope.active ||
		sv_vr_axe_trace_scope.mode != SV_VR_AXE_TRACE_SCOPE_DWELL)
		return;
	if (qcvm != &sv.qcvm || !qcvm->progs || !pr_global_struct ||
		!SV_DwellBerserkAkimboProgramLoaded ())
	{
		SV_VRStockAxeClearTraceScope ();
		return;
	}
	if (qcvm->xfunction == &qcvm->functions[DWELL_W_FIREAXE_FUNCTION])
	{
		SV_VRStockAxeClearTraceScope ();
		return;
	}
	if (qcvm->xfunction != &qcvm->functions[DWELL_TRACELINE2_FUNCTION] ||
		qcvm->xstatement != 13044 || qcvm->depth <= 0)
		return;

	caller = &qcvm->stack[qcvm->depth - 1];
	if (caller->f != &qcvm->functions[DWELL_W_FIREAXE_FUNCTION] ||
		caller->s != 14578 || !sv_vr_axe_trace_scope.dwell_has_contact)
		return;
	if (!sv_vr_axe_trace_scope.dwell_force_miss ||
		sv_vr_axe_trace_scope.dwell_invalidated ||
		!sv_vr_axe_trace_scope.player || sv_vr_axe_trace_scope.player->free ||
		!SV_DwellBerserkAkimboWeaponSelected (sv_vr_axe_trace_scope.player) ||
		pr_global_struct->self != EDICT_TO_PROG (sv_vr_axe_trace_scope.player) ||
		G_INT (7680) != pr_global_struct->self ||
		G_FLOAT (7681) != 0 || G_FLOAT (7689) != 1 ||
		pr_global_struct->trace_startsolid || pr_global_struct->trace_allsolid)
	{
		sv_vr_axe_trace_scope.dwell_invalidated = true;
		return;
	}

	contact = &sv_vr_axe_trace_scope.trace;
	if (!contact->ent || contact->ent->free ||
		!isfinite (contact->fraction) || contact->fraction < 0 ||
		contact->fraction >= 1 || contact->startsolid || contact->allsolid ||
		pr_global_struct->trace_ent != EDICT_TO_PROG (contact->ent))
	{
		sv_vr_axe_trace_scope.dwell_invalidated = true;
		return;
	}
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (contact->endpos[axis]) ||
			pr_global_struct->trace_endpos[axis] != contact->endpos[axis])
		{
			sv_vr_axe_trace_scope.dwell_invalidated = true;
			return;
		}
	pr_global_struct->trace_fraction = contact->fraction;
}

void SV_ResetPrivateVRContactState (client_t *client)
{
	if (!client)
		return;
	client->private_vr_contact_last_sequence = 0;
	client->private_vr_contact_cursor_valid = false;
	client->private_vr_contact_spawn_seen = false;
	SV_ResetPrivateVRContactContinuity (client);
}

/* A relocation invalidates every contact sample already accepted before it.
 * Movement still owns and retires those commands; only the contact cursor
 * advances. Resetting history alone would let a later queued pair rearm at the
 * new origin in the same frame. */
static void SV_VRContactInvalidateAccepted (client_t *client)
{
	if (!client)
		return;
	SV_ResetPrivateVRContactContinuity (client);
	if (!client->private_vr_contact_cursor_valid ||
		client->lastmovemessage > client->private_vr_contact_last_sequence)
		client->private_vr_contact_last_sequence = client->lastmovemessage;
	client->private_vr_contact_cursor_valid = true;
}

void SV_VRContactPlayerRelocated (edict_t *ent)
{
	int slot;
	if (!ent)
		return;
	if (qcvm == &sv.qcvm)
	{
		slot = NUM_FOR_EDICT (ent);
		if (slot >= 1 && slot <= svs.maxclients &&
			svs.clients[slot - 1].edict == ent)
		{
			SV_GorillaInvalidateAccepted (&svs.clients[slot - 1]);
			SV_VRContactInvalidateAccepted (&svs.clients[slot - 1]);
		}
		return;
	}
	/* Host commands can place a player without an active server VM. */
	for (slot = 0; slot < svs.maxclients; slot++)
		if (svs.clients[slot].edict == ent)
		{
			SV_GorillaInvalidateAccepted (&svs.clients[slot]);
			SV_VRContactInvalidateAccepted (&svs.clients[slot]);
			return;
		}
}

void SV_VRContactPlayerSetOrigin (edict_t *ent, const vec3_t origin)
{
	vec3_t delta;
	if (!ent || !origin)
		return;
	VectorSubtract (ent->v.origin, origin, delta);
	if (VectorLength (delta) > 0.01f)
		SV_VRContactPlayerRelocated (ent);
}

static qboolean SV_CoopRespawnPointContentsOK (vec3_t origin,
	edict_t *ent, qboolean allow_water)
{
	int i, cont;
	vec3_t point;
	float checks[3];

	checks[0] = ent->v.mins[2] + 1.0f;
	checks[1] = 0.0f;
	checks[2] = ent->v.maxs[2] - 1.0f;
	for (i = 0; i < countof (checks); i++)
	{
		VectorCopy (origin, point);
		point[2] += checks[i];
		cont = SV_PointContents (point);
		if (cont == CONTENTS_SOLID || cont == CONTENTS_LAVA ||
			cont == CONTENTS_SLIME || (!allow_water && cont == CONTENTS_WATER))
			return false;
	}
	return true;
}

static qboolean SV_CoopRespawnTriggerLooksHazard (edict_t *touch)
{
	const char *classname;

	if (!touch || touch->free || touch->v.solid != SOLID_TRIGGER ||
		!touch->v.touch || !touch->v.classname)
		return false;
	classname = PR_GetString (touch->v.classname);
	if (!classname || !classname[0])
		return false;
	return q_strcasestr (classname, "hurt") ||
		q_strcasestr (classname, "kill") ||
		q_strcasestr (classname, "void") ||
		q_strcasestr (classname, "death") ||
		q_strcasestr (classname, "lava") ||
		q_strcasestr (classname, "slime");
}

static qboolean SV_CoopRespawnTouchesHazardTrigger (edict_t *ent,
	vec3_t origin)
{
	int i;
	vec3_t mins, maxs;

	VectorAdd (origin, ent->v.mins, mins);
	VectorAdd (origin, ent->v.maxs, maxs);
	for (i = svs.maxclients + 1; i < qcvm->num_edicts; i++)
	{
		edict_t *touch = EDICT_NUM (i);
		if (!SV_CoopRespawnTriggerLooksHazard (touch))
			continue;
		if (mins[0] > touch->v.absmax[0] || mins[1] > touch->v.absmax[1] ||
			mins[2] > touch->v.absmax[2] || maxs[0] < touch->v.absmin[0] ||
			maxs[1] < touch->v.absmin[1] || maxs[2] < touch->v.absmin[2])
			continue;
		return true;
	}
	return false;
}

static qboolean SV_CoopRespawnCanPlaceAt (edict_t *ent,
	vec3_t origin, qboolean allow_water)
{
	qboolean bottom;
	trace_t trace;
	vec3_t old_origin;

	if (!SV_CoopRespawnPointContentsOK (origin, ent, allow_water))
		return false;
	trace = SV_Move (origin, ent->v.mins, ent->v.maxs, origin, MOVE_NORMAL, ent);
	if (trace.allsolid || trace.startsolid ||
		SV_CoopRespawnTouchesHazardTrigger (ent, origin))
		return false;
	VectorCopy (ent->v.origin, old_origin);
	VectorCopy (origin, ent->v.origin);
	bottom = SV_CheckBottom (ent);
	VectorCopy (old_origin, ent->v.origin);
	return bottom;
}

static qboolean SV_CoopRespawnAllowWater (edict_t *ent)
{
	int entnum;
	client_t *client;

	if (!ent || ent->free)
		return true;
	entnum = NUM_FOR_EDICT (ent);
	if (entnum < 1 || entnum > svs.maxclients)
		return true;
	client = &svs.clients[entnum - 1];
	return client->edict != ent || !SV_PrivateWalkTrialSelected (client);
}

static qboolean SV_CoopRespawnDropToFloor (edict_t *ent,
	vec3_t origin, float max_drop, qboolean allow_water,
	vec3_t floor_origin)
{
	int i;
	trace_t trace;
	vec3_t start, end;
	static const float raises[] = {96.0f, 64.0f, 48.0f, 32.0f, 16.0f, 8.0f};

	for (i = 0; i < countof (raises); i++)
	{
		VectorCopy (origin, start);
		start[2] += raises[i];
		VectorCopy (start, end);
		end[2] -= 384.0f;
		trace = SV_Move (start, ent->v.mins, ent->v.maxs, end,
			MOVE_NORMAL, ent);
		if (trace.allsolid || trace.startsolid || trace.fraction == 1.0f)
			continue;
		VectorCopy (trace.endpos, floor_origin);
		if (max_drop > 0.0f && floor_origin[2] < origin[2] - max_drop)
			continue;
		if (SV_CoopRespawnCanPlaceAt (ent, floor_origin, allow_water))
			return true;
	}
	return false;
}

static void SV_CoopRespawnBasis (edict_t *anchor, vec3_t forward,
	vec3_t right)
{
	vec3_t up;

	if (anchor)
	{
		AngleVectors (anchor->v.angles, forward, right, up);
		forward[2] = right[2] = 0.0f;
		if (VectorNormalize (forward) < 0.01f)
		{
			forward[0] = 1.0f;
			forward[1] = forward[2] = 0.0f;
		}
		if (VectorNormalize (right) < 0.01f)
		{
			right[0] = 0.0f;
			right[1] = -1.0f;
			right[2] = 0.0f;
		}
	}
	else
	{
		forward[0] = 1.0f;
		forward[1] = forward[2] = 0.0f;
		right[0] = right[2] = 0.0f;
		right[1] = 1.0f;
	}
}

static qboolean SV_CoopRespawnFindNearbySpot (edict_t *ent,
	vec3_t base, edict_t *anchor, const float *radii, int num_radii,
	float max_drop, qboolean allow_water, vec3_t spot)
{
	int i, j;
	vec3_t candidate, dropped, forward, right;
	static const float dirs[][2] = {
		{0.0f, 0.0f}, {1.0f, 0.0f}, {0.9239f, 0.3827f},
		{0.7071f, 0.7071f}, {0.3827f, 0.9239f}, {0.0f, 1.0f},
		{-0.3827f, 0.9239f}, {-0.7071f, 0.7071f}, {-0.9239f, 0.3827f},
		{-1.0f, 0.0f}, {-0.9239f, -0.3827f}, {-0.7071f, -0.7071f},
		{-0.3827f, -0.9239f}, {0.0f, -1.0f}, {0.3827f, -0.9239f},
		{0.7071f, -0.7071f}, {0.9239f, -0.3827f}
	};

	SV_CoopRespawnBasis (anchor, forward, right);
	for (i = 0; i < num_radii; i++)
		for (j = 0; j < countof (dirs); j++)
		{
			if (radii[i] > 0.0f && dirs[j][0] == 0.0f && dirs[j][1] == 0.0f)
				continue;
			if (radii[i] == 0.0f && j > 0)
				continue;
			VectorCopy (base, candidate);
			candidate[0] += (forward[0] * dirs[j][0] + right[0] * dirs[j][1]) * radii[i];
			candidate[1] += (forward[1] * dirs[j][0] + right[1] * dirs[j][1]) * radii[i];
			if (!SV_CoopRespawnDropToFloor (ent, candidate, max_drop,
				allow_water, dropped))
				continue;
			VectorCopy (dropped, spot);
			return true;
		}
	return false;
}

static void SV_CoopRespawnRemoveSpawnTeledeath (edict_t *owner)
{
	int i;

	if (!owner || owner->free || !qcvm || !qcvm->progs)
		return;
	for (i = svs.maxclients + 1; i < qcvm->num_edicts; i++)
	{
		edict_t *ent = EDICT_NUM (i);
		const char *classname;
		if (ent->free || !ent->v.classname)
			continue;
		classname = PR_GetString (ent->v.classname);
		if (!classname || strcmp (classname, "teledeath") ||
			PROG_TO_EDICT (ent->v.owner) != owner)
			continue;
		ED_Free (ent);
	}
}

static void SV_CoopRespawnRelocate (edict_t *ent, edict_t *anchor,
	vec3_t spot)
{
	vec3_t angles;

	/* Drop contacts accepted at the old origin before linking the new one. */
	SV_VRContactPlayerRelocated (ent);
	SV_CoopRespawnRemoveSpawnTeledeath (ent);
	VectorCopy (spot, ent->v.origin);
	VectorClear (ent->v.velocity);
	angles[0] = angles[2] = 0.0f;
	angles[1] = anchor->v.angles[1];
	VectorCopy (angles, ent->v.angles);
	VectorCopy (angles, ent->v.v_angle);
	ent->v.fixangle = true;
	SV_LinkEdict (ent, false);
	SV_PrivatePlayerTeleported (ent);
}

qboolean SV_CoopRespawnTeleportToPlayer (edict_t *ent, edict_t *target)
{
	static const float radii[] = {0.0f, 40.0f, 48.0f, 64.0f, 80.0f, 96.0f, 128.0f};
	vec3_t spot;
	qboolean allow_water;

	if (!coop.value || deathmatch.value || !ent || ent->free || !target ||
		target->free || ent == target || !SV_CoopRespawnIsAliveClient (ent) ||
		!SV_CoopRespawnIsAliveClient (target))
		return false;
	allow_water = SV_CoopRespawnAllowWater (ent);
	if (!SV_CoopRespawnFindNearbySpot (ent, target->v.origin, target, radii,
		countof (radii), 384.0f, allow_water, spot))
	{
		if (!SV_CoopFeatureEnabled (&sv_coop_player_teleport_fallback, true) ||
			!SV_CoopFeatureEnabled (&sv_coop_noplayerclip, true) ||
			!SV_CoopFeatureEnabled (&sv_coop_notelefrag, true))
			return false;
		if (!allow_water && !SV_CoopRespawnCanPlaceAt (ent, target->v.origin,
			false))
			return false;
		VectorCopy (target->v.origin, spot);
	}
	SV_CoopRespawnRelocate (ent, target, spot);
	return true;
}

qboolean SV_CoopRespawnTeleportToSpawn (edict_t *ent, edict_t *spawn)
{
	static const float radii[] = {0.0f, 32.0f, 48.0f, 64.0f, 80.0f, 96.0f, 128.0f};
	vec3_t base, spot;
	qboolean allow_water;

	if (!coop.value || deathmatch.value || !ent || ent->free || !spawn ||
		spawn->free || !SV_CoopRespawnIsAliveClient (ent))
		return false;
	allow_water = SV_CoopRespawnAllowWater (ent);
	VectorCopy (spawn->v.origin, base);
	base[2] += 1.0f;
	if (!SV_CoopRespawnFindNearbySpot (ent, base, spawn, radii,
		countof (radii), 128.0f, allow_water, spot))
		return false;
	SV_CoopRespawnRelocate (ent, spawn, spot);
	return true;
}

static float SV_VRContactDistance (const vec3_t a, const vec3_t b)
{
	vec3_t delta;
	VectorSubtract (a, b, delta);
	return VectorLength (delta);
}

static qboolean SV_VRContactBodyOrigin (edict_t *ent, vec3_t origin)
{
	sv_vr_weapon_pose_scope_t *scope = SV_FindPrivateVRWeaponPose (ent);
	if (scope)
	{
		if (!scope->applied || scope->origin_relocated ||
			scope->akimbo_invalidated)
			return false;
		VectorCopy (scope->body_origin, origin);
	}
	else
		VectorCopy (ent->v.origin, origin);
	return SV_EnyoVectorIsFinite (origin);
}

static void SV_VRContactFeedback (client_t *client, int hand)
{
	char command[48];
	int command_size;

	if (!client || (hand != 0 && hand != 1) || !client->message.data ||
		client->message.overflowed ||
		client->message.cursize < 0 || client->message.maxsize < 0 ||
		client->message.cursize > client->message.maxsize)
		return;
	command_size = q_snprintf (command, sizeof (command),
		"//vr_weapon_contact_haptic %d\n", hand);
	if (command_size < 0 || (size_t)command_size >= sizeof (command) ||
		command_size + 2 > client->message.maxsize - client->message.cursize)
		return;

	MSG_WriteByte (&client->message, svc_stufftext);
	MSG_WriteString (&client->message, command);
}

static qboolean SV_VRContactOwnerLive (client_t *client, edict_t *ent)
{
	return client && ent && client->active && client->spawned &&
		client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		client->edict == ent && !ent->free && isfinite (ent->v.health) &&
		ent->v.health > 0 && !ent->v.deadflag;
}

static qboolean SV_VRContactWeaponIdentity (edict_t *ent,
	const vr_weapon_contact_t *contact)
{
	const char *weaponmodel;
	int axis;

	if (!ent || ent->free || !contact || contact->modelindex <= 0 ||
		contact->modelindex >= MAX_MODELS || !sv.model_precache[contact->modelindex] ||
		!isfinite (ent->v.weapon) || ent->v.weapon < 0 || ent->v.weapon > 4096 ||
		!isfinite (contact->weapon) || contact->weapon < 0 ||
		contact->weapon > 4096 || floorf (contact->weapon) != contact->weapon ||
		contact->weapon != ent->v.weapon)
		return false;
	weaponmodel = PR_GetString (ent->v.weaponmodel);
	if (!weaponmodel || !weaponmodel[0] ||
		strcmp (sv.model_precache[contact->modelindex], weaponmodel))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (ent->v.origin[axis]) || fabsf (ent->v.origin[axis]) > 1000000.0f ||
			!isfinite (ent->v.view_ofs[axis]) || fabsf (ent->v.view_ofs[axis]) > 128.0f)
			return false;
	return true;
}

/* A paired command is admitted from current server state, never from the
 * previously advertised capability mask. The bilateral contact and berserk
 * pair must describe the same command and selected source viewmodel. */
static qboolean SV_VRDwellBerserkPairSelected (client_t *client, edict_t *ent,
	const usercmd_t *cmd)
{
	const unsigned int flags = VR_WEAPON_CONTACT_LEFT_VALID |
		VR_WEAPON_CONTACT_RIGHT_VALID | VR_WEAPON_CONTACT_IMMERSIVE_MELEE;
	int hand;

	if (!cmd || !SV_VRDwellBerserkMeleeEnabled () ||
		!SV_VRContactOwnerLive (client, ent) ||
		!SV_DwellBerserkAkimboWeaponSelected (ent) ||
		!cmd->vr_akimbo_berserk ||
		!SV_AkimboCommandValid (client, cmd, ent->v.origin) ||
		cmd->vr_contact.flags != flags ||
		!SV_VRContactWeaponIdentity (ent, &cmd->vr_contact))
		return false;
	for (hand = 0; hand < 2; hand++)
		if (SV_VRContactDistance (cmd->vr_contact.base[hand],
			cmd->vr_contact.tip[hand]) > 32.0f)
			return false;
	return true;
}

static qboolean SV_VRDirectMeleeContactSelected (client_t *client, edict_t *ent,
	const usercmd_t *cmd, int *subtype)
{
	const unsigned int hands_mask = VR_WEAPON_CONTACT_LEFT_VALID |
		VR_WEAPON_CONTACT_RIGHT_VALID;
	const unsigned int flags = cmd ? cmd->vr_contact.flags : 0;
	const unsigned int hands = flags & hands_mask;
	int selected_subtype;
	int hand;

	if (!cmd ||
		!SV_VRContactOwnerLive (client, ent) ||
		!SV_VRDirectMeleeSelected (ent, &selected_subtype) ||
		!(selected_subtype == SV_VR_DIRECT_MELEE_ENYO_SWORD ?
			SV_VREnyoMeleeEnabled () : SV_VRQBJ3MeleeEnabled ()) ||
		!(flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) ||
		!SV_VRContactWeaponIdentity (ent, &cmd->vr_contact))
		return false;
	if (selected_subtype == SV_VR_DIRECT_MELEE_QBJ3_BERSERK)
	{
		if (hands != hands_mask || !cmd->vr_akimbo_berserk ||
			!SV_AkimboCommandValid (client, cmd, ent->v.origin))
			return false;
	}
	else if ((hands != VR_WEAPON_CONTACT_LEFT_VALID &&
		hands != VR_WEAPON_CONTACT_RIGHT_VALID) ||
		cmd->vr_akimbo_active || cmd->vr_akimbo_berserk)
		return false;
	for (hand = 0; hand < 2; hand++)
		if ((hands & (1u << hand)) &&
			SV_VRContactDistance (cmd->vr_contact.base[hand],
				cmd->vr_contact.tip[hand]) > 32.0f)
			return false;
	if (subtype)
		*subtype = selected_subtype;
	return true;
}

static qboolean SV_VRContactSampleValid (client_t *client, edict_t *ent,
	const usercmd_t *cmd)
{
	const vr_weapon_contact_t *contact = &cmd->vr_contact;
	unsigned int hand_flags = VR_WEAPON_CONTACT_LEFT_VALID |
		VR_WEAPON_CONTACT_RIGHT_VALID;
	unsigned int hands = contact->flags & hand_flags;
	qboolean melee = (contact->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) != 0;
	int hand, axis;

	if (!SV_VRContactOwnerLive (client, ent) ||
		sv.paused || !cmd->vr_active || !cmd->vr_handpos_relative ||
		cmd->msec < 1 || cmd->msec > 125 || cmd->sequence <= 0 ||
		!isfinite (client->lastmovetime) || client->lastmovetime <= 0 ||
		realtime < client->lastmovetime ||
		realtime - client->lastmovetime > SV_VR_CONTACT_MAX_PLAYER_AGE ||
		!isfinite (cmd->vr_contact_received) || cmd->vr_contact_received < 0 ||
		realtime < cmd->vr_contact_received ||
		realtime - cmd->vr_contact_received > SV_VR_CONTACT_MAX_FRESHNESS ||
		(contact->flags & ~VR_WEAPON_CONTACT_KNOWN_FLAGS) || !hands ||
		!SV_VRContactWeaponIdentity (ent, contact))
		return false;
	if (melee &&
		!((SV_VRStockAxeContactProfile () !=
			VR_WEAPON_CONTACT_PROFILE_NONE &&
			(hands == VR_WEAPON_CONTACT_LEFT_VALID ||
			 hands == VR_WEAPON_CONTACT_RIGHT_VALID) &&
			SV_VRStockAxeMeleeEnabled ()) ||
		  SV_VRDwellBerserkPairSelected (client, ent, cmd) ||
		  SV_VRDirectMeleeContactSelected (client, ent, cmd, NULL)))
		return false;
	if (!melee && !SV_VRWeaponCollisionEnabled ())
		return false;
	if (melee)
		for (axis = 0; axis < 3; axis++)
			if (!isfinite (cmd->vr_handrot[axis]) ||
				fabsf (cmd->vr_handrot[axis]) > 3600.0f)
				return false;

	/* Other contact samples remain ordinary physical-hand queries. */
	for (hand = 0; hand < 2; hand++)
	{
		if (!(contact->flags & (1u << hand)))
			continue;
		if (!isfinite (contact->speed[hand]) || contact->speed[hand] < 0 ||
			contact->speed[hand] > SV_VR_CONTACT_MAX_SPEED)
			return false;
		for (axis = 0; axis < 3; axis++)
			if (!isfinite (contact->grip[hand][axis]) ||
				!isfinite (contact->base[hand][axis]) ||
				!isfinite (contact->tip[hand][axis]) ||
				fabsf (contact->grip[hand][axis]) > SV_VR_CONTACT_MAX_OFFSET ||
				fabsf (contact->base[hand][axis]) > SV_VR_CONTACT_MAX_OFFSET ||
				fabsf (contact->tip[hand][axis]) > SV_VR_CONTACT_MAX_OFFSET)
				return false;
		if (SV_VRContactDistance (contact->grip[hand], vec3_origin) > SV_VR_CONTACT_MAX_OFFSET ||
			SV_VRContactDistance (contact->base[hand], contact->grip[hand]) > SV_VR_CONTACT_MAX_OFFSET ||
			SV_VRContactDistance (contact->tip[hand], contact->grip[hand]) > SV_VR_CONTACT_MAX_OFFSET ||
			SV_VRContactDistance (contact->base[hand], contact->tip[hand]) > SV_VR_CONTACT_MAX_OFFSET)
			return false;
	}
	return true;
}

static qboolean SV_VRContactCommandValid (client_t *client, edict_t *ent,
	const usercmd_t *cmd)
{
	return cmd && !cmd->impulse &&
		SV_VRContactSampleValid (client, ent, cmd);
}

static qboolean SV_VRContactSameSample (const vr_weapon_contact_t *a,
	const vr_weapon_contact_t *b)
{
	int hand, axis;
	if (a->flags != b->flags || a->modelindex != b->modelindex ||
		a->weapon != b->weapon)
		return false;
	for (hand = 0; hand < 2; hand++)
	{
		if (a->speed[hand] != b->speed[hand])
			return false;
		for (axis = 0; axis < 3; axis++)
			if (a->grip[hand][axis] != b->grip[hand][axis] ||
				a->base[hand][axis] != b->base[hand][axis] ||
				a->tip[hand][axis] != b->tip[hand][axis])
				return false;
	}
	return true;
}

/* Keep suppression and contact processing on the same continuity contract.
 * A NULL previous sample validates the first pose after continuity reset. */
static qboolean SV_VRContactTransitionValid (client_t *client, edict_t *ent,
	const usercmd_t *cmd, const vr_weapon_contact_t *previous,
	double previous_received, const vec3_t previous_body_origin)
{
	const vr_weapon_contact_t *sample;
	vec3_t current_body_origin;
	float seconds;
	int hand;

	if (!SV_VRContactCommandValid (client, ent, cmd) ||
		!SV_VRContactBodyOrigin (ent, current_body_origin))
		return false;
	if (!previous)
		return true;
	sample = &cmd->vr_contact;
	seconds = cmd->msec * 0.001f;
	if (!isfinite (previous_received) ||
		cmd->vr_contact_received < previous_received ||
		cmd->vr_contact_received - previous_received >
			SV_VR_CONTACT_MAX_FRESHNESS ||
		sample->modelindex != previous->modelindex ||
		sample->weapon != previous->weapon ||
		sample->flags != previous->flags ||
		SV_VRContactDistance (current_body_origin,
			previous_body_origin) > 64.0f)
		return false;

	for (hand = 0; hand < 2; hand++)
	{
		float point_motion;
		vec3_t old_point, new_point;

		if (!(sample->flags & (1u << hand)))
			continue;
		VectorCopy (previous->grip[hand], old_point);
		VectorCopy (sample->grip[hand], new_point);
		point_motion = SV_VRContactDistance (old_point, new_point);
		VectorCopy (previous->base[hand], old_point);
		VectorCopy (sample->base[hand], new_point);
		point_motion = fmaxf (point_motion,
			SV_VRContactDistance (old_point, new_point));
		VectorCopy (previous->tip[hand], old_point);
		VectorCopy (sample->tip[hand], new_point);
		point_motion = fmaxf (point_motion,
			SV_VRContactDistance (old_point, new_point));
		if (point_motion > 32.0f ||
			point_motion > sample->speed[hand] * seconds * 80.0f + 3.0f)
			return false;
	}
	return true;
}

static qboolean SV_VRStockAxeSelected (client_t *client, edict_t *ent,
	const usercmd_t *cmd, int *hand)
{
	const sv_vr_stock_axe_descriptor_t *descriptor =
		SV_VRStockAxeMeleeDescriptor ();
	const vr_weapon_contact_t *contact = &cmd->vr_contact;
	unsigned int hands = contact->flags &
		(VR_WEAPON_CONTACT_LEFT_VALID | VR_WEAPON_CONTACT_RIGHT_VALID);
	const char *weaponmodel;
	int active_hand;

	if (!descriptor || !SV_VRStockAxeMeleeEnabled () ||
		SV_VRStockAxeContactProfile () == VR_WEAPON_CONTACT_PROFILE_NONE ||
		!SV_VRContactOwnerLive (client, ent) ||
		!(contact->flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) ||
		(hands != VR_WEAPON_CONTACT_LEFT_VALID &&
		 hands != VR_WEAPON_CONTACT_RIGHT_VALID) ||
		!isfinite (ent->v.weapon) || ent->v.weapon != descriptor->weapon_bit)
		return false;
	weaponmodel = PR_GetString (ent->v.weaponmodel);
	if (!weaponmodel || strcmp (weaponmodel, descriptor->alkaline ?
		"progs/v_alkaxe20fps.mdl" : "progs/v_axe.mdl"))
		return false;
	active_hand = hands == VR_WEAPON_CONTACT_LEFT_VALID ? 0 : 1;
	if (SV_VRContactDistance (contact->base[active_hand],
		contact->tip[active_hand]) > 32.0f)
		return false;
	if (hand)
		*hand = active_hand;
	return true;
}

static qboolean SV_VRStockAxeReady (client_t *client, edict_t *ent,
	const usercmd_t *cmd)
{
	const sv_vr_stock_axe_descriptor_t *descriptor =
		SV_VRStockAxeMeleeDescriptor ();
	eval_t *cooldown;
	float qctime;

	if (!descriptor || !SV_VRStockAxeSelected (client, ent, cmd, NULL) ||
		!isfinite (qcvm->time) || !isfinite (ent->v.nextthink))
		return false;
	if (ent->v.think && ent->v.nextthink > 0 &&
		ent->v.think != descriptor->stand_index &&
		ent->v.think != descriptor->run_index)
		return false;
	if (descriptor->alkaline &&
		ent->v.think != descriptor->stand_index &&
		ent->v.think != descriptor->run_index)
		return false;
	cooldown = GetEdictFieldValue (ent,
		ED_FindFieldOffset ("attack_finished"));
	qctime = (float)qcvm->time;
	return cooldown && isfinite (cooldown->_float) &&
		cooldown->_float <= qctime;
}

static qboolean SV_VRDwellBerserkReady (client_t *client, edict_t *ent,
	const usercmd_t *cmd)
{
	eval_t *cooldown, *customflags;
	int customflags_offset;
	float qctime;

	if (!SV_VRDwellBerserkPairSelected (client, ent, cmd) ||
		!isfinite (qcvm->time) ||
		(ent->v.think != DWELL_PLAYER_STAND_FUNCTION &&
		 ent->v.think != DWELL_PLAYER_RUN_FUNCTION))
		return false;
	qctime = (float)qcvm->time;
	if (!isfinite (qctime))
		return false;
	customflags_offset = ED_FindFieldOffset ("customflags");
	customflags = customflags_offset >= 0 ?
		GetEdictFieldValue (ent, customflags_offset) : NULL;
	cooldown = GetEdictFieldValue (ent,
		ED_FindFieldOffset ("attack_finished"));
	return customflags && isfinite (customflags->_float) &&
		(double)customflags->_float >= -2147483648.0 &&
		(double)customflags->_float < 2147483648.0 &&
		((int)customflags->_float & 2112) == 0 &&
		cooldown && isfinite (cooldown->_float) &&
		cooldown->_float <= qctime;
}

static qboolean SV_VRContactEyeGripClear (edict_t *ent,
	const vr_weapon_contact_t *contact, int hand)
{
	vec3_t eye, grip;
	trace_t trace;
	VectorAdd (ent->v.origin, ent->v.view_ofs, eye);
	VectorAdd (ent->v.origin, contact->grip[hand], grip);
	trace = SV_Move (eye, vec3_origin, vec3_origin, grip,
		MOVE_NOMONSTERS, ent);
	return !trace.startsolid && !trace.allsolid && trace.fraction >= 1.0f;
}

/* Suppress a duplicate QC trigger only when every retained record from the
 * contact cursor to this command is contiguous and carries a fresh, valid
 * immersive sample. A queue gap leaves the ordinary trigger path intact. */
static qboolean SV_VRMeleeSuppressNativeTrigger (client_t *client,
	edict_t *ent, const usercmd_t *cmd)
{
	unsigned int offset;
	int expected;
	qboolean found = false;
	vr_weapon_contact_t previous;
	vec3_t previous_body_origin;
	vec3_t current_body_origin;
	double previous_received = 0;
	qboolean previous_valid;

	if (!client || !cmd || !(cmd->buttons & BUTTON_ATTACK) ||
		cmd->impulse ||
		!(SV_VRStockAxeSelected (client, ent, cmd, NULL) ||
		  SV_VRDwellBerserkPairSelected (client, ent, cmd) ||
		  SV_VRDirectMeleeContactSelected (client, ent, cmd, NULL)) ||
		!SV_VRContactCommandValid (client, ent, cmd) ||
		!SV_VRContactBodyOrigin (ent, current_body_origin) ||
		!client->private_vr_contact_cursor_valid ||
		client->private_discarded_move > client->private_vr_contact_last_sequence ||
		client->private_retired_move > client->private_vr_contact_last_sequence)
		return false;

	/* The most recently accepted fresh pose owns the trigger during idle and
	 * cooldown frames too. It must still be the exact accepted contact sample. */
	if (client->private_vr_contact_previous_valid &&
		(int)cmd->sequence == client->private_vr_contact_last_sequence &&
		cmd->vr_contact_received == client->private_vr_contact_previous_received &&
		SV_VRContactSameSample (&cmd->vr_contact,
			&client->private_vr_contact_previous) &&
		SV_VRContactDistance (current_body_origin,
			client->private_vr_contact_body_origin) <= 64.0f)
		return true;

	if (client->private_cmd_queue_count > SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_head >= SV_PRIVATE_CMD_QUEUE_SIZE ||
		(int)cmd->sequence <= client->private_vr_contact_last_sequence)
		return false;

	expected = client->private_vr_contact_last_sequence + 1;
	previous_valid = client->private_vr_contact_previous_valid;
	VectorClear (previous_body_origin);
	if (previous_valid)
	{
		previous = client->private_vr_contact_previous;
		VectorCopy (client->private_vr_contact_body_origin, previous_body_origin);
		previous_received = client->private_vr_contact_previous_received;
	}
	for (offset = 0; offset < client->private_cmd_queue_count; offset++)
	{
		const usercmd_t *queued = &client->private_cmd_queue[
			(client->private_cmd_queue_head + offset) % SV_PRIVATE_CMD_QUEUE_SIZE];
		if ((int)queued->sequence < expected)
			continue;
		if ((int)queued->sequence > (int)cmd->sequence)
			break;
		if ((int)queued->sequence != expected ||
			!(queued->vr_contact.flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE) ||
			!(SV_VRStockAxeSelected (client, ent, queued, NULL) ||
			  SV_VRDwellBerserkPairSelected (client, ent, queued) ||
			  SV_VRDirectMeleeContactSelected (client, ent, queued,
				NULL)) ||
			!SV_VRContactTransitionValid (client, ent, queued,
				previous_valid ? &previous : NULL, previous_received,
				previous_body_origin))
			return false;
		previous = queued->vr_contact;
		previous_received = queued->vr_contact_received;
		VectorCopy (current_body_origin, previous_body_origin);
		previous_valid = true;
		if ((int)queued->sequence == (int)cmd->sequence)
		{
			found = true;
			break;
		}
		expected++;
	}
	return found && previous_valid &&
		cmd->vr_contact_received == previous_received &&
		SV_VRContactSameSample (&cmd->vr_contact, &previous);
}

static void SV_VRMeleeRefreshTriggerSuppression (client_t *client,
	edict_t *ent, const usercmd_t *cmd, qboolean *suppressed)
{
	if (!*suppressed)
		return;
	if (SV_VRMeleeSuppressNativeTrigger (client, ent, cmd))
		ent->v.button0 = 0;
	else
	{
		*suppressed = false;
		ent->v.button0 = (cmd->buttons & BUTTON_ATTACK) != 0;
	}
}

typedef enum
{
	SV_VR_AXE_SWEEP_STOCK,
	SV_VR_AXE_SWEEP_DWELL_EDGE,
	SV_VR_AXE_SWEEP_DIRECT_EDGE
} sv_vr_axe_sweep_policy_t;

/* Recover an edge point that is already inside a collider by finding that
 * collider's surface from the attacker's side. A startsolid trace itself has
 * no usable entry surface; never reinterpret it as a hit or extend the edge
 * to an unrelated intervening target. */
static qboolean SV_VRDwellRecoverAxeOverlap (edict_t *ent,
	vec3_t eye, vec3_t grip, vec3_t start, vec3_t end,
	trace_t *trace)
{
	vec3_t embedded, anchor;
	int attempt;

	VectorCopy (start, embedded);
	for (attempt = 0; attempt < 2; attempt++)
	{
		trace_t entry, inside, reach;
		VectorCopy (attempt ? eye : grip, anchor);
		entry = SV_Move (anchor, vec3_origin, vec3_origin, embedded,
			MOVE_NORMAL, ent);
		if (entry.startsolid || entry.allsolid || entry.fraction >= 1.0f ||
			!entry.ent || entry.ent->free)
			continue;
		inside = SV_ClipMoveToEntity (entry.ent, embedded, vec3_origin,
			vec3_origin, embedded, CONTENTMASK_ANYSOLID);
		if (!inside.startsolid)
			continue;
		reach = SV_Move (grip, vec3_origin, vec3_origin, entry.endpos,
			MOVE_NOMONSTERS, ent);
		if (reach.startsolid || reach.allsolid ||
			(reach.fraction < 1.0f &&
			 SV_VRContactDistance (reach.endpos, entry.endpos) > 2.0f))
			continue;
		VectorCopy (anchor, start);
		VectorCopy (embedded, end);
		*trace = entry;
		return true;
	}
	return false;
}

static qboolean SV_VRAxeSweep (edict_t *ent,
	const vr_weapon_contact_t *previous, const vr_weapon_contact_t *current,
	int hand, sv_vr_axe_sweep_policy_t policy, float min_time,
	const int *excluded, int excluded_count, trace_t *best,
	float *best_time, qboolean *blocked)
{
	vec3_t eye, grip;
	float first_fraction = FLT_MAX;
	float first_blocked_fraction = FLT_MAX;
	qboolean found = false;
	int best_part = -1, best_point = -1;
	int first_part = policy == SV_VR_AXE_SWEEP_STOCK ? 0 : 1;
	int part;

	memset (best, 0, sizeof (*best));
	best->fraction = 1.0f;
	*blocked = false;
	if (best_time)
		*best_time = 2.0f;
	if (!isfinite (min_time) || min_time < 0 || min_time > 1 ||
		excluded_count < 0 || excluded_count > 2 ||
		(excluded_count && !excluded))
		return false;
	VectorAdd (ent->v.origin, ent->v.view_ofs, eye);
	VectorAdd (ent->v.origin, current->grip[hand], grip);
	{
		trace_t reach = SV_Move (eye, vec3_origin, vec3_origin, grip,
			MOVE_NOMONSTERS, ent);
		if (reach.startsolid || reach.allsolid || reach.fraction < 1.0f)
		{
			*blocked = true;
			return false;
		}
	}

	/* Stock covers the handle (grip to head) and edge (base to tip); the
	 * Dwell policy starts at the edge. Historical offsets stay in the
	 * current body frame so walking cannot generate a weapon stroke. */
	for (part = first_part; part < 2; part++)
	{
		const vec_t *old_a = part == 0 ? previous->grip[hand] : previous->base[hand];
		const vec_t *old_b = part == 0 ? previous->base[hand] : previous->tip[hand];
		const vec_t *new_a = part == 0 ? current->grip[hand] : current->base[hand];
		const vec_t *new_b = part == 0 ? current->base[hand] : current->tip[hand];
		float old_length = SV_VRContactDistance (old_a, old_b);
		float new_length = SV_VRContactDistance (new_a, new_b);
		int steps = CLAMP (1, (int)ceilf (fmaxf (old_length, new_length) / 3.0f), 32);
		int point;

		/* The current shaft is an endpoint fallback after all swept points.
		 * Its trace fraction is spatial, so its event time is one. */
		for (point = -1; point <= steps; point++)
		{
			vec3_t start, end, old_point, new_point, impact;
			trace_t candidate, reach;
			qboolean recovered = false;
			float t = point < 0 ? 0.0f : (float)point / steps;
			float event_time;
			int axis;

			for (axis = 0; axis < 3; axis++)
			{
				old_point[axis] = point < 0 ? new_a[axis] :
					old_a[axis] + t * (old_b[axis] - old_a[axis]);
				new_point[axis] = point < 0 ? new_b[axis] :
					new_a[axis] + t * (new_b[axis] - new_a[axis]);
				start[axis] = ent->v.origin[axis] + old_point[axis];
				end[axis] = ent->v.origin[axis] + new_point[axis];
				if (point >= 0)
					start[axis] += min_time * (end[axis] - start[axis]);
			}
			candidate = SV_Move (start, vec3_origin, vec3_origin, end,
				MOVE_NORMAL, ent);
			if (candidate.startsolid || candidate.allsolid)
			{
				if (policy == SV_VR_AXE_SWEEP_STOCK ||
					!SV_VRDwellRecoverAxeOverlap (ent, eye, grip, start, end,
						&candidate))
					continue;
				recovered = true;
			}
			if (candidate.fraction >= 1.0f || !candidate.ent || candidate.ent->free ||
				candidate.ent == ent)
				continue;
			event_time = point < 0 ? 1.0f : recovered ? min_time :
				min_time + (1.0f - min_time) * candidate.fraction;
			/* A previous victim stays solid in every real trace. It is only
			 * excluded from being another outcome of this same stroke. */
			qboolean already_hit = false;
			for (int excluded_index = 0; excluded_index < excluded_count;
				excluded_index++)
				if (excluded[excluded_index] == NUM_FOR_EDICT (candidate.ent))
					already_hit = true;
			if (already_hit)
				continue;

			VectorCopy (candidate.endpos, impact);
			reach = SV_Move (grip, vec3_origin, vec3_origin, impact,
				MOVE_NOMONSTERS, ent);
			if (reach.startsolid || reach.allsolid ||
				(reach.fraction < 1.0f &&
				 (reach.ent != candidate.ent ||
				  SV_VRContactDistance (reach.endpos, candidate.endpos) > 2.0f)))
			{
				if (event_time < first_blocked_fraction)
					first_blocked_fraction = event_time;
				continue;
			}
			/* Point traces share a temporal fraction; the current shaft occurs
			 * at interval end. Exact ties use stable handle-to-edge order. */
			if (!found || event_time < first_fraction ||
				(event_time == first_fraction &&
				 (part < best_part ||
				  (part == best_part && point < best_point))))
			{
				*best = candidate;
				first_fraction = event_time;
				best_part = part;
				best_point = point;
				found = true;
			}
		}
	}
	/* A later reachable contact cannot erase a nearer failure of the grip-to-hit
	 * reach trace. Equal-time obstruction wins conservatively. */
	*blocked = first_blocked_fraction < FLT_MAX &&
		first_blocked_fraction <= first_fraction;
	if (found && best_time)
		*best_time = first_fraction;
	return found;
}

static qboolean SV_VRStockAxeSweep (edict_t *ent,
	const vr_weapon_contact_t *previous, const vr_weapon_contact_t *current,
	int hand, trace_t *best, qboolean *blocked)
{
	return SV_VRAxeSweep (ent, previous, current, hand,
		SV_VRStockAxeContactProfile () == VR_WEAPON_CONTACT_PROFILE_ALK ?
		SV_VR_AXE_SWEEP_DWELL_EDGE : SV_VR_AXE_SWEEP_STOCK,
		0, NULL, 0, best, NULL, blocked);
}

static qboolean SV_VRStockAxeVMStorageValid (qcvm_t *saved_vm,
	dprograms_t *saved_progs, edict_t *saved_edicts,
	globalvars_t *saved_global_struct, float *saved_vm_globals)
{
	return qcvm == saved_vm && qcvm == &sv.qcvm &&
		qcvm->progs == saved_progs && qcvm->edicts == saved_edicts &&
		qcvm->globals == saved_vm_globals &&
		pr_global_struct == saved_global_struct;
}

static qboolean SV_VRStockAxeOutcome (client_t *client, edict_t *ent,
	const usercmd_t *cmd, const trace_t *contact)
{
	const sv_vr_stock_axe_descriptor_t *descriptor =
		SV_VRStockAxeMeleeDescriptor ();
	globalvars_t saved_globals;
	float saved_call_globals[OFS_PARM7 + 3 - OFS_RETURN];
	qcvm_t *saved_vm;
	dprograms_t *saved_progs;
	edict_t *saved_edicts;
	globalvars_t *saved_global_struct;
	float *saved_vm_globals;
	eval_t *cooldown, *hostile;
	int saved_argc;
	vec3_t saved_angles;
	qboolean rogue, friendly_fire_scope;
	qboolean alive = false;

	if (!descriptor || !SV_VRStockAxeReady (client, ent, cmd) ||
		(contact && (!contact->ent || contact->ent->free ||
			contact->fraction < 0 || contact->fraction >= 1.0f)))
		return false;
	cooldown = GetEdictFieldValue (ent,
		ED_FindFieldOffset ("attack_finished"));
	hostile = GetEdictFieldValue (ent,
		ED_FindFieldOffset ("show_hostile"));
	if (!cooldown || !hostile || !isfinite (hostile->_float))
		return false;
	rogue = descriptor->progscrc == 54028;

	saved_vm = qcvm;
	saved_progs = qcvm->progs;
	saved_edicts = qcvm->edicts;
	saved_global_struct = pr_global_struct;
	saved_vm_globals = qcvm->globals;
	saved_globals = *pr_global_struct;
	saved_argc = qcvm->argc;
	memcpy (saved_call_globals, qcvm->globals + OFS_RETURN,
		sizeof (saved_call_globals));
	VectorCopy (ent->v.v_angle, saved_angles);
	SV_VRStockAxeClearTraceScope ();
	friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
	VectorCopy (cmd->vr_handrot, ent->v.v_angle);
	ent->v.v_angle[ROLL] = 0;
	AngleVectors (ent->v.v_angle, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;

	/* W_Attack normally owns these side effects before entering its leaf.
	 * Keep the native cues and timing while replacing only physical targeting. */
	hostile->_float = descriptor->alkaline ? 0.0f : qcvm->time + 1.0f;
	if (descriptor->alkaline)
		SV_StartSound (ent, ent->v.origin, 1, "weapons/ax1.wav", 255, 1);
	if (!rogue && !descriptor->alkaline)
		cooldown->_float = qcvm->time + 0.5f;
	if (!descriptor->alkaline)
	{
		PR_ExecuteProgram (descriptor->sound_index);
		if (!SV_VRStockAxeVMStorageValid (saved_vm, saved_progs,
			saved_edicts, saved_global_struct, saved_vm_globals))
			goto cleanup;
	}
	if (rogue)
	{
		G_INT (OFS_PARM0) = EDICT_TO_PROG (ent);
		qcvm->argc = 1;
		PR_ExecuteProgram (122); /* RuneApplyBlackNoise(self) */
		if (!SV_VRStockAxeVMStorageValid (saved_vm, saved_progs,
			saved_edicts, saved_global_struct, saved_vm_globals))
			goto cleanup;
		qcvm->argc = 0;
	}
	if (!descriptor->alkaline)
		SV_StartSound (ent, ent->v.origin, 1, "weapons/ax1.wav", 255, 1);
	if (rogue)
	{
		G_FLOAT (OFS_PARM0) = 0.5f;
		G_INT (OFS_PARM1) = EDICT_TO_PROG (ent);
		qcvm->argc = 2;
		PR_ExecuteProgram (124); /* RuneApplyHell(.5, self) */
		if (!SV_VRStockAxeVMStorageValid (saved_vm, saved_progs,
			saved_edicts, saved_global_struct, saved_vm_globals))
			goto cleanup;
		cooldown->_float = qcvm->time + G_FLOAT (OFS_RETURN);
		qcvm->argc = 0;
	}
	if (descriptor->alkaline)
	{
		cooldown->_float = qcvm->time + 0.5f;
		PR_ExecuteProgram (descriptor->sound_index);
		if (!SV_VRStockAxeVMStorageValid (saved_vm, saved_progs,
			saved_edicts, saved_global_struct, saved_vm_globals))
			goto cleanup;
	}
	if (contact && !ent->free && ent->v.health > 0 && !ent->v.deadflag)
	{
		sv_vr_axe_trace_scope.mode = SV_VR_AXE_TRACE_SCOPE_STOCK;
		sv_vr_axe_trace_scope.client = client;
		sv_vr_axe_trace_scope.player = ent;
		sv_vr_axe_trace_scope.function =
			&qcvm->functions[descriptor->leaf_index];
		sv_vr_axe_trace_scope.trace = *contact;
		sv_vr_axe_trace_scope.active = true;
		PR_ExecuteProgram (descriptor->leaf_index);
		SV_VRStockAxeClearTraceScope ();
	}

	if (SV_VRStockAxeVMStorageValid (saved_vm, saved_progs,
		saved_edicts, saved_global_struct, saved_vm_globals))
	{
		qcvm->argc = saved_argc;
		memcpy (qcvm->globals + OFS_RETURN, saved_call_globals,
			sizeof (saved_call_globals));
		if (client->edict == ent && !ent->free)
			VectorCopy (saved_angles, ent->v.v_angle);
		/* Match the donor's borrowed-QC context boundary: retain gameplay
		 * globals written by the native leaf, restoring only its call context,
		 * basis and transient trace result. */
		pr_global_struct->self = saved_globals.self;
		pr_global_struct->other = saved_globals.other;
		pr_global_struct->time = saved_globals.time;
		VectorCopy (saved_globals.v_forward, pr_global_struct->v_forward);
		VectorCopy (saved_globals.v_right, pr_global_struct->v_right);
		VectorCopy (saved_globals.v_up, pr_global_struct->v_up);
		pr_global_struct->trace_allsolid = saved_globals.trace_allsolid;
		pr_global_struct->trace_startsolid = saved_globals.trace_startsolid;
		pr_global_struct->trace_fraction = saved_globals.trace_fraction;
		pr_global_struct->trace_inwater = saved_globals.trace_inwater;
		pr_global_struct->trace_inopen = saved_globals.trace_inopen;
		pr_global_struct->trace_plane_dist = saved_globals.trace_plane_dist;
		pr_global_struct->trace_ent = saved_globals.trace_ent;
		VectorCopy (saved_globals.trace_endpos, pr_global_struct->trace_endpos);
		VectorCopy (saved_globals.trace_plane_normal,
			pr_global_struct->trace_plane_normal);
		alive = client->active && client->spawned && client->edict == ent &&
			!ent->free;
	}
	else
		SV_VRStockAxeClearTraceScope ();

cleanup:
	SV_VRStockAxeClearTraceScope ();
	if (friendly_fire_scope)
		SV_CoopFriendlyFireEnd ();
	return alive;
}

static qboolean SV_VRDwellPhysicalOutcomeVMOwnerValid (client_t *client,
	edict_t *ent, qcvm_t *saved_vm, dprograms_t *saved_progs,
	globalvars_t *saved_global_struct, float *saved_vm_globals)
{
	if (!saved_vm || qcvm != saved_vm || qcvm != &sv.qcvm ||
		qcvm->progs != saved_progs || qcvm->globals != saved_vm_globals ||
		pr_global_struct != saved_global_struct || !saved_progs ||
		!pr_global_struct || !SV_DwellBerserkAkimboProgramLoaded ())
		return false;
	return SV_VRContactOwnerLive (client, ent) &&
		SV_DwellBerserkAkimboWeaponSelected (ent);
}

static qboolean SV_VRDwellPhysicalOutcomeContextValid (client_t *client,
	edict_t *ent, qcvm_t *saved_vm, dprograms_t *saved_progs,
	globalvars_t *saved_global_struct, float *saved_vm_globals,
	const vec3_t body_origin, qboolean cursor_valid, int cursor_sequence)
{
	eval_t *customflags;
	int axis, customflags_offset;

	if (!SV_VRDwellPhysicalOutcomeVMOwnerValid (client, ent, saved_vm,
		saved_progs, saved_global_struct, saved_vm_globals) ||
		client->private_vr_contact_cursor_valid != cursor_valid ||
		client->private_vr_contact_last_sequence != cursor_sequence ||
		(ent->v.think != DWELL_PLAYER_STAND_FUNCTION &&
		 ent->v.think != DWELL_PLAYER_RUN_FUNCTION))
		return false;
	for (axis = 0; axis < 3; ++axis)
		if (!isfinite (ent->v.origin[axis]) ||
			ent->v.origin[axis] != body_origin[axis])
			return false;
	customflags_offset = ED_FindFieldOffset ("customflags");
	customflags = customflags_offset >= 0 ?
		GetEdictFieldValue (ent, customflags_offset) : NULL;
	return customflags && isfinite (customflags->_float) &&
		(double)customflags->_float >= -2147483648.0 &&
		(double)customflags->_float < 2147483648.0 &&
		((int)customflags->_float & 2112) == 0;
}

qboolean SV_VRDwellBerserkPhysicalOutcome (client_t *client, edict_t *ent,
	const usercmd_t *cmd, int anatomical_hand, const trace_t *contact)
{
	globalvars_t saved_globals, *saved_global_struct;
	float saved_call_globals[OFS_PARM7 + 3 - OFS_RETURN];
	qcvm_t *saved_vm;
	dprograms_t *saved_progs;
	float *saved_vm_globals;
	eval_t *customflags, *cooldown;
	trace_t accepted_contact;
	vec3_t accepted_angles, saved_angles, body_origin;
	float qctime, haste_value = 0, new_cooldown;
	int saved_argc, axis, cursor_sequence;
	qboolean has_contact = contact != NULL;
	qboolean haste, context_saved = false, outcome_ok = false;
	qboolean friendly_fire_scope = false;
	qboolean cursor_valid;

	/* This owner never nests with another physical axe trace. Retire stale
	 * state on every admission and callback failure path. */
	SV_VRStockAxeClearTraceScope ();
	if (!cmd || (anatomical_hand != 0 && anatomical_hand != 1) ||
		!SV_DwellBerserkAkimboProgramLoaded () ||
		!SV_VRContactOwnerLive (client, ent) ||
		!SV_DwellBerserkAkimboWeaponSelected (ent) ||
		!cmd->vr_active || !cmd->vr_handpos_relative ||
		!cmd->vr_akimbo_active || !cmd->vr_akimbo_berserk ||
		(ent->v.think != DWELL_PLAYER_STAND_FUNCTION &&
		 ent->v.think != DWELL_PLAYER_RUN_FUNCTION) ||
		!isfinite (qcvm->time))
		goto cleanup;
	qctime = (float)qcvm->time;
	if (!isfinite (qctime))
		goto cleanup;
	VectorCopy (ent->v.origin, body_origin);
	cursor_valid = client->private_vr_contact_cursor_valid;
	cursor_sequence = client->private_vr_contact_last_sequence;
	for (axis = 0; axis < 3; ++axis)
	{
		if (!isfinite (body_origin[axis]))
			goto cleanup;
		accepted_angles[axis] = cmd->vr_akimbo_angles[anatomical_hand][axis];
		if (!isfinite (accepted_angles[axis]))
			goto cleanup;
	}

	customflags = GetEdictFieldValue (ent, ED_FindFieldOffset ("customflags"));
	cooldown = GetEdictFieldValue (ent, ED_FindFieldOffset ("attack_finished"));
	if (!customflags || !isfinite (customflags->_float) ||
		(double)customflags->_float < -2147483648.0 ||
		(double)customflags->_float >= 2147483648.0 ||
		((int)customflags->_float & 2112) != 0 ||
		!cooldown || !isfinite (cooldown->_float) ||
		cooldown->_float > qctime)
		goto cleanup;
	if (contact)
	{
		accepted_contact = *contact;
		if (!accepted_contact.ent || accepted_contact.ent->free ||
			!isfinite (accepted_contact.fraction) ||
			accepted_contact.fraction < 0 || accepted_contact.fraction >= 1 ||
			accepted_contact.startsolid || accepted_contact.allsolid ||
			!isfinite (accepted_contact.endpos[0]) ||
			!isfinite (accepted_contact.endpos[1]) ||
			!isfinite (accepted_contact.endpos[2]) ||
			!isfinite (accepted_contact.plane.dist) ||
			!isfinite (accepted_contact.plane.normal[0]) ||
			!isfinite (accepted_contact.plane.normal[1]) ||
			!isfinite (accepted_contact.plane.normal[2]))
			goto cleanup;
	}

	if (!qcvm->globals || !pr_global_struct)
		goto cleanup;
	saved_vm = qcvm;
	saved_progs = qcvm->progs;
	saved_global_struct = pr_global_struct;
	saved_vm_globals = qcvm->globals;
	saved_globals = *pr_global_struct;
	saved_argc = qcvm->argc;
	memcpy (saved_call_globals, qcvm->globals + OFS_RETURN,
		sizeof (saved_call_globals));
	VectorCopy (ent->v.v_angle, saved_angles);
	context_saved = true;

	VectorCopy (accepted_angles, ent->v.v_angle);
	AngleVectors (ent->v.v_angle, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram (DWELL_SUPER_DAMAGE_SOUND_FUNCTION);
	if (!SV_VRDwellPhysicalOutcomeContextValid (client, ent, saved_vm,
		saved_progs, saved_global_struct, saved_vm_globals, body_origin,
		cursor_valid, cursor_sequence))
		goto cleanup;

	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram (DWELL_BERSERK_SOUND_FUNCTION);
	if (!SV_VRDwellPhysicalOutcomeContextValid (client, ent, saved_vm,
		saved_progs, saved_global_struct, saved_vm_globals, body_origin,
		cursor_valid, cursor_sequence))
		goto cleanup;

	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	G_INT (OFS_PARM0) = EDICT_TO_PROG (ent);
	qcvm->argc = 1;
	PR_ExecuteProgram (DWELL_HAS_HASTE_FUNCTION);
	if (!SV_VRDwellPhysicalOutcomeContextValid (client, ent, saved_vm,
		saved_progs, saved_global_struct, saved_vm_globals, body_origin,
		cursor_valid, cursor_sequence))
		goto cleanup;
	haste_value = G_FLOAT (OFS_RETURN);
	if (!isfinite (haste_value))
		goto cleanup;
	haste = haste_value != 0;

	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	PR_ExecuteProgram (DWELL_AXE_WHIFF_SOUND_FUNCTION);
	if (!SV_VRDwellPhysicalOutcomeContextValid (client, ent, saved_vm,
		saved_progs, saved_global_struct, saved_vm_globals, body_origin,
		cursor_valid, cursor_sequence))
		goto cleanup;
	qctime = (float)qcvm->time;
	cooldown = GetEdictFieldValue (ent, ED_FindFieldOffset ("attack_finished"));
	if (!isfinite (qctime) || !cooldown || !isfinite (cooldown->_float))
		goto cleanup;
	new_cooldown = qctime + .49f * (haste ? .6f : 1.0f);
	if (!isfinite (new_cooldown))
		goto cleanup;
	cooldown->_float = new_cooldown;
	if (!SV_VRDwellPhysicalOutcomeContextValid (client, ent, saved_vm,
		saved_progs, saved_global_struct, saved_vm_globals, body_origin,
		cursor_valid, cursor_sequence))
		goto cleanup;

	/* The Dwell leaf consumes the accepted trace once. A NULL contact is an
	 * explicit physical whiff, so its first helper acquisition is a miss. */
	sv_vr_axe_trace_scope.mode = SV_VR_AXE_TRACE_SCOPE_DWELL;
	sv_vr_axe_trace_scope.client = client;
	sv_vr_axe_trace_scope.player = ent;
	sv_vr_axe_trace_scope.function =
		&qcvm->functions[DWELL_W_FIREAXE_FUNCTION];
	if (has_contact)
		sv_vr_axe_trace_scope.trace = accepted_contact;
	else
		memset (&sv_vr_axe_trace_scope.trace, 0,
			sizeof (sv_vr_axe_trace_scope.trace));
	sv_vr_axe_trace_scope.dwell_has_contact = has_contact;
	sv_vr_axe_trace_scope.dwell_force_miss = !has_contact;
	sv_vr_axe_trace_scope.active = true;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
	PR_ExecuteProgram (DWELL_W_FIREAXE_FUNCTION);
	if (friendly_fire_scope)
		SV_CoopFriendlyFireEnd ();
	if (SV_VRDwellPhysicalOutcomeVMOwnerValid (client, ent, saved_vm,
		saved_progs, saved_global_struct, saved_vm_globals))
		outcome_ok = true;

cleanup:
	SV_VRStockAxeClearTraceScope ();
	if (context_saved && qcvm == saved_vm && qcvm->progs == saved_progs &&
		qcvm->globals == saved_vm_globals &&
		pr_global_struct == saved_global_struct)
	{
		qcvm->argc = saved_argc;
		memcpy (qcvm->globals + OFS_RETURN, saved_call_globals,
			sizeof (saved_call_globals));
		if (!ent->free)
			VectorCopy (saved_angles, ent->v.v_angle);
		pr_global_struct->self = saved_globals.self;
		pr_global_struct->other = saved_globals.other;
		pr_global_struct->time = saved_globals.time;
		VectorCopy (saved_globals.v_forward, pr_global_struct->v_forward);
		VectorCopy (saved_globals.v_right, pr_global_struct->v_right);
		VectorCopy (saved_globals.v_up, pr_global_struct->v_up);
		pr_global_struct->trace_allsolid = saved_globals.trace_allsolid;
		pr_global_struct->trace_startsolid = saved_globals.trace_startsolid;
		pr_global_struct->trace_fraction = saved_globals.trace_fraction;
		pr_global_struct->trace_inwater = saved_globals.trace_inwater;
		pr_global_struct->trace_inopen = saved_globals.trace_inopen;
		pr_global_struct->trace_plane_dist = saved_globals.trace_plane_dist;
		pr_global_struct->trace_ent = saved_globals.trace_ent;
		VectorCopy (saved_globals.trace_endpos, pr_global_struct->trace_endpos);
		VectorCopy (saved_globals.trace_plane_normal,
			pr_global_struct->trace_plane_normal);
	}
	return outcome_ok;
}

/* The installed draw loop returns without an attack once frame 10 is reached;
 * any other scheduled weapon think may still fire a native melee stroke. */
static qboolean SV_VRQBJ3MeleeIdle (edict_t *ent)
{
	return isfinite (ent->v.nextthink) &&
		(ent->v.think == 569 ||
		 (ent->v.think == 575 && isfinite (ent->v.weaponframe) &&
		  ent->v.weaponframe >= 10));
}

static qboolean SV_VRDirectMeleeIdle (edict_t *ent, int subtype)
{
	const int think = ent->v.think;
	if (subtype != SV_VR_DIRECT_MELEE_ENYO_SWORD)
		return SV_VRQBJ3MeleeIdle (ent);
	if (!isfinite (ent->v.nextthink))
		return false;
	/* Match the donor's unscheduled-state and harmless-aftermath policy.
	 * Sword4 performs the native strike; conservatively keep all scheduled
	 * sword1..9 frames under native ownership. */
	if (!think || ent->v.nextthink <= 0)
		return true;
	if ((think == 506 && SV_DwellFunctionPin (506, "player_stand1",
		21638, 0, 0, 0, NULL)) ||
		(think == 507 && SV_DwellFunctionPin (507, "player_run",
		21699, 7490, 1, 0, NULL)))
		return true;
	if (think >= 560 && think <= 572)
	{
		static const char *const names[] = {
			"player_sword10", "player_sword11", "player_sword12",
			"player_sword13", "player_sword14", "player_sword15",
			"player_sword16", "player_sword17", "player_swordhit1",
			"player_swordhit2", "player_swordhit3", "player_swordhit4",
			"player_swordhit5"
		};
		return SV_DwellFunctionPin (think, names[think - 560],
			22188 + (think - 560) * 7, 0, 0, 0, NULL);
	}
	return false;
}

static qboolean SV_VRDirectMeleeOutcomeContextValid (client_t *client, edict_t *ent,
	qcvm_t *saved_vm, dprograms_t *saved_progs, edict_t *saved_edicts,
	globalvars_t *saved_global_struct, float *saved_vm_globals,
	const vec3_t body_origin, qboolean cursor_valid, int cursor_sequence,
	int subtype, qboolean first_outcome)
{
	int selected_subtype;
	int axis;

	if (qcvm != saved_vm || qcvm != &sv.qcvm ||
		qcvm->progs != saved_progs || qcvm->edicts != saved_edicts ||
		qcvm->globals != saved_vm_globals ||
		pr_global_struct != saved_global_struct ||
		!SV_VRContactOwnerLive (client, ent) ||
		!SV_VRDirectMeleeSelected (ent, &selected_subtype) ||
		selected_subtype != subtype ||
		client->private_vr_contact_cursor_valid != cursor_valid ||
		client->private_vr_contact_last_sequence != cursor_sequence ||
		(first_outcome && !SV_VRDirectMeleeIdle (ent, subtype)))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite (ent->v.origin[axis]) ||
			ent->v.origin[axis] != body_origin[axis])
			return false;
	return true;
}

/* Borrow the selected mod's explicit hit leaf for one physical outcome. The
 * first outcome owns sound/recovery and may whiff. It returns the live subtype
 * and deadline. A follow-up can only hit; its caller must authorize a distinct
 * second victim before that deadline, using the existing per-hand contact
 * owner and passing back the saved subtype. A first whiff ends the stroke;
 * the caller also owns VM identity, victim deduplication and the two-hit cap.
 * The adapter never enters the attack fan/root or installs a think. Enyo's
 * native leaf may install its harmless hit aftermath, which remains intact.
 * The queued contact caller owns the two-victim stroke state below. */
static qboolean SV_VRDirectMeleeOutcome
	(client_t *client, edict_t *ent,
	const usercmd_t *cmd, int anatomical_hand, const trace_t *contact,
	qboolean first_outcome, int *stroke_subtype,
	float *recovery_deadline)
{
	globalvars_t saved_globals, *saved_global_struct;
	float saved_call_globals[OFS_PARM7 + 3 - OFS_RETURN];
	qcvm_t *saved_vm;
	dprograms_t *saved_progs;
	edict_t *saved_edicts;
	float *saved_vm_globals;
	eval_t *cooldown, *hostile, *berserk_sound = NULL, *switchblock = NULL;
	trace_t accepted_contact;
	vec3_t accepted_angles, saved_angles, body_origin, org, dir;
	float qctime, new_cooldown, new_hostile;
	int axis, saved_argc, cursor_sequence, subtype;
	qboolean berserk, enyo, cursor_valid, context_saved = false;
	qboolean friendly_fire_scope = false, outcome_ok = false;

	if (!cmd || !stroke_subtype ||
		(anatomical_hand != 0 && anatomical_hand != 1) ||
		(first_outcome ? !recovery_deadline : !contact) ||
		!SV_VRContactOwnerLive (client, ent) ||
		!SV_VRDirectMeleeSelected (ent, &subtype) ||
		(!first_outcome && *stroke_subtype != subtype) ||
		!cmd->vr_active || !cmd->vr_handpos_relative ||
		(subtype == SV_VR_DIRECT_MELEE_QBJ3_BERSERK && (!cmd->vr_akimbo_active ||
			!cmd->vr_akimbo_berserk)) ||
		!isfinite (qcvm->time) ||
		(first_outcome && !SV_VRDirectMeleeIdle (ent, subtype)))
		goto cleanup;
	berserk = subtype == SV_VR_DIRECT_MELEE_QBJ3_BERSERK;
	enyo = subtype == SV_VR_DIRECT_MELEE_ENYO_SWORD;
	qctime = (float)qcvm->time;
	if (!isfinite (qctime))
		goto cleanup;
	VectorCopy (ent->v.origin, body_origin);
	cursor_valid = client->private_vr_contact_cursor_valid;
	cursor_sequence = client->private_vr_contact_last_sequence;
	for (axis = 0; axis < 3; axis++)
	{
		if (!isfinite (body_origin[axis]))
			goto cleanup;
		accepted_angles[axis] = berserk ?
			cmd->vr_akimbo_angles[anatomical_hand][axis] :
			cmd->vr_handrot[axis];
		if (!isfinite (accepted_angles[axis]) ||
			fabsf (accepted_angles[axis]) > SV_VR_AKIMBO_MAX_ANGLE)
			goto cleanup;
	}
	cooldown = GetEdictFieldValue (ent,
		ED_FindFieldOffset ("attack_finished"));
	hostile = GetEdictFieldValue (ent,
		ED_FindFieldOffset ("show_hostile"));
	if ((first_outcome && (!cooldown || !isfinite (cooldown->_float) ||
		cooldown->_float > qctime)) || !hostile ||
		!isfinite (hostile->_float))
		goto cleanup;
	if (first_outcome && berserk)
	{
		berserk_sound = GetEdictFieldValue (ent,
			ED_FindFieldOffset ("berserk_sound"));
		if (!berserk_sound || !isfinite (berserk_sound->_float))
			goto cleanup;
	}
	if (first_outcome && enyo)
	{
		switchblock = GetEdictFieldValue (ent,
			ED_FindFieldOffset ("switchblock_finished"));
		if (!switchblock || !isfinite (switchblock->_float))
			goto cleanup;
	}
	new_cooldown = first_outcome ? qctime + (enyo ? .4f : berserk ? .5f : .8f) : 0;
	new_hostile = qctime + 1.0f;
	if (!isfinite (new_cooldown) || !isfinite (new_hostile))
		goto cleanup;
	if (contact)
	{
		accepted_contact = *contact;
		if (!accepted_contact.ent || accepted_contact.ent->free ||
			accepted_contact.ent == ent ||
			!isfinite (accepted_contact.fraction) ||
			accepted_contact.fraction < 0 ||
			accepted_contact.fraction >= 1 ||
			accepted_contact.startsolid || accepted_contact.allsolid ||
			!isfinite (accepted_contact.endpos[0]) ||
			!isfinite (accepted_contact.endpos[1]) ||
			!isfinite (accepted_contact.endpos[2]) ||
			!isfinite (accepted_contact.plane.dist) ||
			!isfinite (accepted_contact.plane.normal[0]) ||
			!isfinite (accepted_contact.plane.normal[1]) ||
			!isfinite (accepted_contact.plane.normal[2]))
			goto cleanup;
	}
	if (!qcvm->globals || !pr_global_struct)
		goto cleanup;
	saved_vm = qcvm;
	saved_progs = qcvm->progs;
	saved_edicts = qcvm->edicts;
	saved_global_struct = pr_global_struct;
	saved_vm_globals = qcvm->globals;
	saved_globals = *pr_global_struct;
	saved_argc = qcvm->argc;
	memcpy (saved_call_globals, qcvm->globals + OFS_RETURN,
		sizeof (saved_call_globals));
	VectorCopy (ent->v.v_angle, saved_angles);
	context_saved = true;

	VectorCopy (accepted_angles, ent->v.v_angle);
	AngleVectors (ent->v.v_angle, pr_global_struct->v_forward,
		pr_global_struct->v_right, pr_global_struct->v_up);
	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	qcvm->argc = 0;
	if (first_outcome)
	{
		cooldown->_float = new_cooldown;
		if (enyo)
		{
			switchblock->_float = new_cooldown;
			SV_StartSound (ent, ent->v.origin, 1, "weapons/sword1.wav", 128, 1);
		}
		else if (!berserk)
		{
			PR_ExecuteProgram (595); /* Native wrench SuperDamageSound. */
			if (!SV_VRDirectMeleeOutcomeContextValid (client, ent, saved_vm,
				saved_progs, saved_edicts, saved_global_struct,
				saved_vm_globals, body_origin, cursor_valid,
				cursor_sequence, subtype, first_outcome))
				goto cleanup;
		}
		if (!enyo)
			SV_StartSound (ent, ent->v.origin, 6,
				"impact/wrench_swing.wav", 255, 1);
		if (berserk && berserk_sound->_float < qctime)
		{
			berserk_sound->_float = new_hostile;
			SV_StartSound (ent, ent->v.origin, 0,
				"items/berserk_fire.wav", 255, 1);
		}
	}
	if (!contact)
	{
		outcome_ok = SV_VRDirectMeleeOutcomeContextValid (client, ent,
			saved_vm, saved_progs, saved_edicts, saved_global_struct,
			saved_vm_globals, body_origin, cursor_valid,
			cursor_sequence, subtype, first_outcome);
		goto cleanup;
	}
	if (!SV_VRDirectMeleeOutcomeContextValid (client, ent, saved_vm,
		saved_progs, saved_edicts, saved_global_struct,
		saved_vm_globals, body_origin, cursor_valid,
		cursor_sequence, subtype, first_outcome) ||
		accepted_contact.ent->free)
		goto cleanup;

	/* Both native leaves consume this direction and trace_ent. Only QBJ3
	 * offsets a sideways brush impact along its normal; Enyo keeps -4 forward. */
	pr_global_struct->self = EDICT_TO_PROG (ent);
	pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
	pr_global_struct->time = qcvm->time;
	VectorMA (accepted_contact.endpos, -4.0f,
		pr_global_struct->v_forward, org);
	if (!enyo && !accepted_contact.ent->v.takedamage &&
		accepted_contact.ent->v.solid == SOLID_BSP &&
		DotProduct (accepted_contact.plane.normal,
			accepted_contact.plane.normal) > .5f &&
		DotProduct (accepted_contact.plane.normal,
			accepted_contact.plane.normal) < 1.5f)
		VectorMA (accepted_contact.endpos, 4.0f,
			accepted_contact.plane.normal, org);
	VectorScale (pr_global_struct->v_right, 20.0f, dir);
	VectorMA (dir, -5.0f, pr_global_struct->v_up, dir);
	pr_global_struct->trace_allsolid = accepted_contact.allsolid;
	pr_global_struct->trace_startsolid = accepted_contact.startsolid;
	pr_global_struct->trace_fraction = accepted_contact.fraction;
	pr_global_struct->trace_inwater = accepted_contact.inwater;
	pr_global_struct->trace_inopen = accepted_contact.inopen;
	pr_global_struct->trace_plane_dist = accepted_contact.plane.dist;
	pr_global_struct->trace_ent = EDICT_TO_PROG (accepted_contact.ent);
	VectorCopy (accepted_contact.endpos, pr_global_struct->trace_endpos);
	VectorCopy (accepted_contact.plane.normal,
		pr_global_struct->trace_plane_normal);
	hostile->_float = new_hostile;
	G_INT (OFS_PARM0) = EDICT_TO_PROG (accepted_contact.ent);
	VectorCopy (org, G_VECTOR (OFS_PARM1));
	VectorCopy (dir, G_VECTOR (OFS_PARM2));
	qcvm->argc = 3;
	friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
	PR_ExecuteProgram (enyo ? 347 : berserk ? 572 : 485);
	if (friendly_fire_scope)
	{
		SV_CoopFriendlyFireEnd ();
		friendly_fire_scope = false;
	}
	outcome_ok = SV_VRDirectMeleeOutcomeContextValid (client, ent, saved_vm,
		saved_progs, saved_edicts, saved_global_struct,
		saved_vm_globals, body_origin, cursor_valid, cursor_sequence,
		subtype, first_outcome);

cleanup:
	if (outcome_ok && first_outcome)
	{
		if (!isfinite (cooldown->_float))
			outcome_ok = false;
		else
		{
			*stroke_subtype = subtype;
			*recovery_deadline = cooldown->_float;
		}
	}
	if (friendly_fire_scope)
		SV_CoopFriendlyFireEnd ();
	if (context_saved && qcvm == saved_vm && qcvm->progs == saved_progs &&
		qcvm->edicts == saved_edicts &&
		qcvm->globals == saved_vm_globals &&
		pr_global_struct == saved_global_struct)
	{
		qcvm->argc = saved_argc;
		memcpy (qcvm->globals + OFS_RETURN, saved_call_globals,
			sizeof (saved_call_globals));
		/* Death rejects another attack, but does not retire this borrowed
		 * pose. Restore the same owned edict even if QC killed its player. */
		if (client->active && client->spawned && client->edict == ent &&
			!ent->free)
			VectorCopy (saved_angles, ent->v.v_angle);
		pr_global_struct->self = saved_globals.self;
		pr_global_struct->other = saved_globals.other;
		pr_global_struct->time = saved_globals.time;
		VectorCopy (saved_globals.v_forward, pr_global_struct->v_forward);
		VectorCopy (saved_globals.v_right, pr_global_struct->v_right);
		VectorCopy (saved_globals.v_up, pr_global_struct->v_up);
		pr_global_struct->trace_allsolid = saved_globals.trace_allsolid;
		pr_global_struct->trace_startsolid = saved_globals.trace_startsolid;
		pr_global_struct->trace_fraction = saved_globals.trace_fraction;
		pr_global_struct->trace_inwater = saved_globals.trace_inwater;
		pr_global_struct->trace_inopen = saved_globals.trace_inopen;
		pr_global_struct->trace_plane_dist = saved_globals.trace_plane_dist;
		pr_global_struct->trace_ent = saved_globals.trace_ent;
		VectorCopy (saved_globals.trace_endpos, pr_global_struct->trace_endpos);
		VectorCopy (saved_globals.trace_plane_normal,
			pr_global_struct->trace_plane_normal);
	}
	return outcome_ok;
}

/* Direct native leaves keep the queued contact cursor, reversal witness and
 * sweep used by Dwell. Their native outcome policy permits two distinct
 * targets before the initial recovery deadline. */
static qboolean SV_VRContactProcessDirectMelee (client_t *client, edict_t *ent,
	const usercmd_t *cmd, const vr_weapon_contact_t *previous, int hand)
{
	const vr_weapon_contact_t *current = &cmd->vr_contact;
	vec3_t movement[2], accepted_direction, body_origin;
	float motion[2], endpoint_motion, length, contact_time = 0;
	float speed = current->speed[hand];
	float seconds = cmd->msec * 0.001f;
	trace_t contact;
	int subtype;
	qboolean blocked, hit, rearming = false;
	qboolean callback_entered = false;
	qcvm_t *saved_vm = qcvm;
	dprograms_t *saved_progs = qcvm->progs;
	edict_t *saved_edicts = qcvm->edicts;
	globalvars_t *saved_global_struct = pr_global_struct;
	float *saved_vm_globals = qcvm->globals;
	int cursor_sequence = client->private_vr_contact_last_sequence;
	int endpoint;

	if (!SV_VRDirectMeleeContactSelected (client, ent, cmd, &subtype))
	{
		client->private_vr_melee_arc[hand] = 0;
		client->private_vr_melee_peak_speed[hand] = 0;
		client->private_vr_melee_consumed[hand] = false;
		VectorClear (client->private_vr_melee_stroke_direction[hand]);
		SV_ResetPrivateVRDirectMeleeStroke (client, hand);
		return false;
	}
	VectorCopy (ent->v.origin, body_origin);
	VectorSubtract (current->base[hand], previous->base[hand], movement[0]);
	VectorSubtract (current->tip[hand], previous->tip[hand], movement[1]);
	motion[0] = VectorLength (movement[0]);
	motion[1] = VectorLength (movement[1]);
	endpoint_motion = fmaxf (motion[0], motion[1]);
	if (client->private_vr_direct_melee_authorized[hand] &&
		(client->private_vr_direct_melee_subtype[hand] != subtype ||
		 !isfinite (qcvm->time) ||
		 !isfinite (client->private_vr_direct_melee_deadline[hand]) ||
		 (float)qcvm->time >= client->private_vr_direct_melee_deadline[hand]))
	{
		SV_ResetPrivateVRDirectMeleeStroke (client, hand);
		client->private_vr_melee_consumed[hand] = true;
	}
	if (client->private_vr_melee_consumed[hand] && speed >= 0.25f)
	{
		endpoint = client->private_vr_melee_stroke_endpoint[hand] ? 1 : 0;
		length = motion[endpoint];
		if (length > 0.0001f &&
			length >= 0.5f * fmaxf (motion[0], motion[1]) &&
			DotProduct (movement[endpoint],
				client->private_vr_melee_stroke_direction[hand]) <
				-0.5f * length)
		{
			client->private_vr_melee_arc[hand] = 0;
			client->private_vr_melee_peak_speed[hand] = 0;
			client->private_vr_melee_consumed[hand] = false;
			SV_ResetPrivateVRDirectMeleeStroke (client, hand);
			rearming = true;
		}
	}
	if (!rearming && speed >= 0.25f && endpoint_motion > 0.0001f)
	{
		client->private_vr_melee_arc[hand] += speed * seconds;
		client->private_vr_melee_peak_speed[hand] = fmaxf (
			client->private_vr_melee_peak_speed[hand], speed);
	}
	if (client->private_vr_melee_consumed[hand] ||
		client->private_vr_melee_arc[hand] < 0.03f ||
		!SV_VRContactEyeGripClear (ent, current, hand))
		goto settle;

	hit = SV_VRAxeSweep (ent, previous, current, hand,
		SV_VR_AXE_SWEEP_DIRECT_EDGE, 0,
		client->private_vr_direct_melee_hit_entities[hand],
		client->private_vr_direct_melee_hit_count[hand], &contact,
		&contact_time, &blocked);
	if (blocked)
		hit = false;
	if (!hit && speed >= 0.05f)
		goto settle;

	endpoint = motion[1] >= motion[0] ? 1 : 0;
	if (speed >= 0.25f && endpoint_motion > 0.0001f)
	{
		VectorCopy (movement[endpoint], accepted_direction);
		if (VectorNormalize (accepted_direction) > 0.0001f)
		{
			client->private_vr_melee_stroke_endpoint[hand] = endpoint;
			VectorCopy (accepted_direction,
				client->private_vr_melee_stroke_direction[hand]);
		}
	}
	for (int outcome = 0; outcome < 2 && !client->private_vr_melee_consumed[hand];
		outcome++)
	{
		const qboolean first = !client->private_vr_direct_melee_authorized[hand];
		const int victim = hit ? NUM_FOR_EDICT (contact.ent) : 0;
		int stroke_subtype = client->private_vr_direct_melee_subtype[hand];
		qboolean accepted;
		float deadline = client->private_vr_direct_melee_deadline[hand];

		if (!first && (!isfinite (qcvm->time) || !isfinite (deadline) ||
			(float)qcvm->time >= deadline ||
			client->private_vr_direct_melee_subtype[hand] != subtype))
		{
			client->private_vr_melee_consumed[hand] = true;
			break;
		}
		if (!first && !hit)
		{
			client->private_vr_melee_consumed[hand] = true;
			break;
		}
		callback_entered = true;
		accepted = SV_VRDirectMeleeOutcome (client, ent, cmd, hand,
			hit ? &contact : NULL, first, &stroke_subtype, &deadline);
		/* A false return may still follow side-effecting native QC. Validate
		 * before writing stroke state, settling it, retracing or entering the
		 * other hand. A continuity reset must not be revived here. */
		if (!SV_VRDirectMeleeOutcomeContextValid (client, ent, saved_vm,
			saved_progs, saved_edicts, saved_global_struct, saved_vm_globals,
			body_origin, true, cursor_sequence, subtype, false) ||
			!client->private_vr_contact_previous_valid ||
			!SV_VRDirectMeleeContactSelected (client, ent, cmd, NULL) ||
			!SV_VRContactSampleValid (client, ent, cmd))
			return true;
		if (accepted && hit && (stroke_subtype != subtype ||
			!isfinite (deadline) || !isfinite (qcvm->time) ||
			(float)qcvm->time >= deadline))
			accepted = false;
		if (!accepted)
		{
			client->private_vr_melee_consumed[hand] = true;
			SV_ResetPrivateVRDirectMeleeStroke (client, hand);
			break;
		}
		if (first)
		{
			client->private_vr_direct_melee_authorized[hand] = hit;
			client->private_vr_direct_melee_subtype[hand] = stroke_subtype;
			client->private_vr_direct_melee_deadline[hand] = deadline;
		}
		if (!hit)
		{
			client->private_vr_melee_consumed[hand] = true;
			break;
		}
		if (client->private_vr_direct_melee_hit_count[hand] < 2)
			client->private_vr_direct_melee_hit_entities[hand][
				client->private_vr_direct_melee_hit_count[hand]++] = victim;
		SV_VRContactFeedback (client, hand);
		if (victim == 0 || client->private_vr_direct_melee_hit_count[hand] >= 2)
		{
			client->private_vr_melee_consumed[hand] = true;
			break;
		}
		/* Previous victims remain solid; query only the remaining motion. */
		hit = SV_VRAxeSweep (ent, previous, current, hand,
			SV_VR_AXE_SWEEP_DIRECT_EDGE, contact_time,
			client->private_vr_direct_melee_hit_entities[hand],
			client->private_vr_direct_melee_hit_count[hand], &contact,
			&contact_time, &blocked);
		if (blocked || !hit)
			break;
	}

settle:
	if (speed < 0.05f ||
		(client->private_vr_melee_consumed[hand] && speed < 0.25f))
	{
		client->private_vr_melee_arc[hand] = 0;
		client->private_vr_melee_peak_speed[hand] = 0;
		client->private_vr_melee_consumed[hand] = false;
		VectorClear (client->private_vr_melee_stroke_direction[hand]);
		SV_ResetPrivateVRDirectMeleeStroke (client, hand);
	}
	return callback_entered;
}

/* Dwell shares the queued contact owner's arc and consumed state. A terminal
 * hit/whiff is consumed before entering QC, including cooldown rejection and
 * callbacks that invalidate the owner after making side effects. */
static qboolean SV_VRContactProcessDwellMelee (client_t *client, edict_t *ent,
	const usercmd_t *cmd, const vr_weapon_contact_t *previous, int hand)
{
	const vr_weapon_contact_t *current = &cmd->vr_contact;
	vec3_t movement[2], accepted_direction;
	float motion[2], endpoint_motion, length;
	float speed = current->speed[hand];
	float seconds = cmd->msec * 0.001f;
	trace_t contact;
	qboolean blocked, hit, terminal, rearming = false;
	qboolean callback_entered = false;
	int endpoint;

	if (!SV_VRDwellBerserkPairSelected (client, ent, cmd))
	{
		client->private_vr_melee_arc[hand] = 0;
		client->private_vr_melee_peak_speed[hand] = 0;
		client->private_vr_melee_consumed[hand] = false;
		VectorClear (client->private_vr_melee_stroke_direction[hand]);
		return false;
	}
	VectorSubtract (current->base[hand], previous->base[hand], movement[0]);
	VectorSubtract (current->tip[hand], previous->tip[hand], movement[1]);
	motion[0] = VectorLength (movement[0]);
	motion[1] = VectorLength (movement[1]);
	endpoint_motion = fmaxf (motion[0], motion[1]);

	if (client->private_vr_melee_consumed[hand] && speed >= 0.25f)
	{
		endpoint = client->private_vr_melee_stroke_endpoint[hand] ? 1 : 0;
		length = motion[endpoint];
		if (length > 0.0001f && length >= 0.5f * endpoint_motion &&
			DotProduct (movement[endpoint],
				client->private_vr_melee_stroke_direction[hand]) <
				-0.5f * length)
		{
			client->private_vr_melee_arc[hand] = 0;
			client->private_vr_melee_peak_speed[hand] = 0;
			client->private_vr_melee_consumed[hand] = false;
			rearming = true;
		}
	}
	if (!rearming && speed >= 0.25f && endpoint_motion > 0.0001f)
	{
		client->private_vr_melee_arc[hand] += speed * seconds;
		client->private_vr_melee_peak_speed[hand] = fmaxf (
			client->private_vr_melee_peak_speed[hand], speed);
	}
	if (client->private_vr_melee_consumed[hand] ||
		client->private_vr_melee_arc[hand] < 0.03f ||
		!SV_VRContactEyeGripClear (ent, current, hand))
		goto settle;

	hit = SV_VRAxeSweep (ent, previous, current, hand,
		SV_VR_AXE_SWEEP_DWELL_EDGE, 0, NULL, 0, &contact, NULL, &blocked);
	if (blocked)
		hit = false;
	terminal = hit || speed < 0.05f;
	if (!terminal)
		goto settle;

	endpoint = motion[1] >= motion[0] ? 1 : 0;
	VectorCopy (movement[endpoint], accepted_direction);
	if (speed >= 0.25f && endpoint_motion > 0.0001f &&
		VectorNormalize (accepted_direction) > 0.0001f)
	{
		client->private_vr_melee_stroke_endpoint[hand] = endpoint;
		VectorCopy (accepted_direction,
			client->private_vr_melee_stroke_direction[hand]);
	}
	client->private_vr_melee_consumed[hand] = true;
	if (SV_VRDwellBerserkReady (client, ent, cmd))
	{
		callback_entered = true;
		if (SV_VRDwellBerserkPhysicalOutcome (client, ent, cmd, hand,
			hit ? &contact : NULL) && hit)
			SV_VRContactFeedback (client, hand);
	}

settle:
	if (speed < 0.05f ||
		(client->private_vr_melee_consumed[hand] && speed < 0.25f))
	{
		client->private_vr_melee_arc[hand] = 0;
		client->private_vr_melee_peak_speed[hand] = 0;
		client->private_vr_melee_consumed[hand] = false;
		VectorClear (client->private_vr_melee_stroke_direction[hand]);
	}
	return callback_entered;
}

static qboolean SV_VRContactProcessMelee (client_t *client, edict_t *ent,
	const usercmd_t *cmd, const vr_weapon_contact_t *previous,
	int hand)
{
	const vr_weapon_contact_t *current = &cmd->vr_contact;
	vec3_t old_point, new_point, movement[2], accepted_direction;
	float endpoint_motion = 0;
	float seconds = cmd->msec * 0.001f;
	trace_t contact;
	qboolean blocked, hit, rearming = false;
	qboolean alkaline = SV_VRStockAxeContactProfile () ==
		VR_WEAPON_CONTACT_PROFILE_ALK;
	int point, endpoint;
	if (SV_VRDirectMeleeContactSelected (client, ent, cmd, NULL))
		return SV_VRContactProcessDirectMelee (client, ent, cmd,
			previous, hand);
	if (SV_VRDwellBerserkPairSelected (client, ent, cmd))
	{
		return SV_VRContactProcessDwellMelee (client, ent, cmd, previous,
			hand);
	}

	if (!SV_VRStockAxeSelected (client, ent, cmd, NULL))
	{
		SV_ResetPrivateVRDirectMeleeStroke (client, hand);
		client->private_vr_melee_arc[hand] = 0;
		client->private_vr_melee_peak_speed[hand] = 0;
		client->private_vr_melee_consumed[hand] = false;
		VectorClear (client->private_vr_melee_stroke_direction[hand]);
		return false;
	}
	if (alkaline && client->private_vr_melee_consumed[hand] &&
		current->speed[hand] < 0.25f)
	{
		/* A completed Alkaline swing rearms below strike speed, even if the
		 * hand never reaches the deeper rest threshold. */
		client->private_vr_melee_arc[hand] = 0;
		client->private_vr_melee_peak_speed[hand] = 0;
		client->private_vr_melee_consumed[hand] = false;
		VectorClear (client->private_vr_melee_stroke_direction[hand]);
		return false;
	}
	if (current->speed[hand] < 0.05f)
	{
		if (!client->private_vr_melee_consumed[hand] &&
			client->private_vr_melee_arc[hand] >= 0.03f &&
			SV_VRStockAxeReady (client, ent, cmd))
		{
			/* A completed armed swing with no contact is the stock axe whiff. */
			client->private_vr_melee_consumed[hand] = true;
			client->private_vr_melee_arc[hand] = 0;
			client->private_vr_melee_peak_speed[hand] = 0;
			VectorClear (client->private_vr_melee_stroke_direction[hand]);
			if (!alkaline)
				client->private_vr_melee_consumed[hand] = false;
			SV_VRStockAxeOutcome (client, ent, cmd, NULL);
			return true;
		}
		client->private_vr_melee_arc[hand] = 0;
		client->private_vr_melee_peak_speed[hand] = 0;
		client->private_vr_melee_consumed[hand] = false;
		VectorClear (client->private_vr_melee_stroke_direction[hand]);
		return false;
	}
	for (point = 0; point < 3; point++)
	{
		const vec_t *old = point == 0 ? previous->grip[hand] :
			point == 1 ? previous->base[hand] : previous->tip[hand];
		const vec_t *now = point == 0 ? current->grip[hand] :
			point == 1 ? current->base[hand] : current->tip[hand];
		VectorCopy (old, old_point);
		VectorCopy (now, new_point);
		endpoint_motion = fmaxf (endpoint_motion,
			SV_VRContactDistance (old_point, new_point));
	}
	if (endpoint_motion <= 0.0001f)
		return false;
	if (alkaline && client->private_vr_melee_consumed[hand])
	{
		float motion[2], length;
		VectorSubtract (current->base[hand], previous->base[hand], movement[0]);
		VectorSubtract (current->tip[hand], previous->tip[hand], movement[1]);
		motion[0] = VectorLength (movement[0]);
		motion[1] = VectorLength (movement[1]);
		endpoint = client->private_vr_melee_stroke_endpoint[hand] ? 1 : 0;
		length = motion[endpoint];
		if (length > 0.0001f && length >= 0.5f * endpoint_motion &&
			DotProduct (movement[endpoint],
				client->private_vr_melee_stroke_direction[hand]) <
				-0.5f * length)
		{
			client->private_vr_melee_arc[hand] = 0;
			client->private_vr_melee_peak_speed[hand] = 0;
			client->private_vr_melee_consumed[hand] = false;
			rearming = true;
		}
	}
	/* A blocked interval is skipped; a consumed Alkaline stroke may still
	 * observe the physical reversal before the next clear interval. */
	if (!SV_VRContactEyeGripClear (ent, current, hand))
		return false;
	if (!rearming && current->speed[hand] >= 0.25f)
	{
		client->private_vr_melee_arc[hand] += current->speed[hand] * seconds;
		client->private_vr_melee_peak_speed[hand] = fmaxf (
			client->private_vr_melee_peak_speed[hand], current->speed[hand]);
	}
	if (client->private_vr_melee_consumed[hand] ||
		client->private_vr_melee_arc[hand] < 0.03f)
		return false;

	/* Cooldown and missed intervals leave the armed stroke live so later
	 * samples can still sweep a target. Only a valid hit commits its outcome. */
	if (!SV_VRStockAxeReady (client, ent, cmd))
		return false;
	hit = SV_VRStockAxeSweep (ent, previous, current, hand, &contact, &blocked);
	if (blocked || !hit)
		return false;
	if (alkaline)
	{
		VectorSubtract (current->base[hand], previous->base[hand], movement[0]);
		VectorSubtract (current->tip[hand], previous->tip[hand], movement[1]);
		endpoint = VectorLength (movement[1]) >= VectorLength (movement[0]) ? 1 : 0;
		VectorCopy (movement[endpoint], accepted_direction);
		if (VectorNormalize (accepted_direction) > 0.0001f)
		{
			client->private_vr_melee_stroke_endpoint[hand] = endpoint;
			VectorCopy (accepted_direction,
				client->private_vr_melee_stroke_direction[hand]);
		}
	}
	client->private_vr_melee_consumed[hand] = true;
	if (SV_VRStockAxeOutcome (client, ent, cmd, &contact))
		SV_VRContactFeedback (client, hand);
	return true;
}

static qboolean SV_VRContactButtonTouchAllowed (edict_t *button,
	int *touch_index)
{
	const char *classname, *function_name;
	dfunction_t *touch;
	int index;

	if (!button || button->free || button->v.solid != SOLID_BSP ||
		!button->v.touch || !isfinite (button->v.health) ||
		button->v.health > 0 || !qcvm->progs ||
		!qcvm->functions)
		return false;
	classname = PR_GetString (button->v.classname);
	if (!classname || strcmp (classname, "func_button"))
		return false;
	touch = ED_FindFunction ("button_touch");
	if (!touch || touch->numparms || touch->first_statement <= 0)
		return false;
	index = (int)(touch - qcvm->functions);
	if (index <= 0 || index >= qcvm->progs->numfunctions ||
		button->v.touch != index)
		return false;
	function_name = PR_GetString (touch->s_name);
	if (!function_name || strcmp (function_name, "button_touch"))
		return false;
	if (touch_index)
		*touch_index = index;
	return true;
}

/* Reuse the stock callback check, then admit only donor-audited AD-family
 * callbacks by exact VM identity. A classname alone is not authorization to
 * invoke arbitrary mod QC from an extra physical contact path. */
static qboolean SV_GorillaButtonTouchAllowed (edict_t *button)
{
	const char *classname, *name;
	dfunction_t *touch;
	int index, statement;
	if (SV_VRContactButtonTouchAllowed (button, NULL))
		return true;
	if (!button || button->free || button->v.solid != SOLID_BSP ||
		!button->v.touch || !isfinite (button->v.health) ||
		button->v.health > 0 || !qcvm->progs || !qcvm->functions)
		return false;
	classname = PR_GetString (button->v.classname);
	if (!classname || strcmp (classname, "func_button"))
		return false;
	switch (qcvm->progscrc)
	{
	case 10963: index = 1790; statement = 85767; break; /* AD */
	case 10710: index = 1514; statement = 69393; break; /* Ravenkeep */
	case 43865: index = 3413; statement = 142718; break; /* Mjolnir */
	default: return false;
	}
	if (index >= qcvm->progs->numfunctions || button->v.touch != index)
		return false;
	touch = &qcvm->functions[index];
	name = PR_GetString (touch->s_name);
	return !touch->numparms && touch->first_statement == statement &&
		name && !strcmp (name, "func_button_touch");
}

typedef struct sv_vr_contact_button_hit_s
{
	edict_t *button;
	float distance;
} sv_vr_contact_button_hit_t;

static void SV_VRContactConsiderButtonTrace (edict_t *player,
	const vec3_t grip, const vec3_t start, const vec3_t end,
	sv_vr_contact_button_hit_t *best)
{
	trace_t trace, reach;
	vec3_t trace_start, trace_end, trace_point, grip_point;
	float distance;

	VectorCopy (start, trace_start);
	VectorCopy (end, trace_end);
	trace = SV_Move (trace_start, vec3_origin, vec3_origin,
		trace_end, MOVE_NORMAL, player);
	if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ||
		!trace.ent || !SV_VRContactButtonTouchAllowed (trace.ent, NULL))
		return;
	VectorCopy (grip, grip_point);
	VectorCopy (trace.endpos, trace_point);
	reach = SV_Move (grip_point, vec3_origin, vec3_origin, trace_point,
		MOVE_NOMONSTERS, player);
	if (reach.startsolid || reach.allsolid ||
		(reach.fraction < 1.0f &&
		(reach.ent != trace.ent ||
		 SV_VRContactDistance (reach.endpos, trace.endpos) > 2.0f)))
		return;
	distance = SV_VRContactDistance (start, trace.endpos);
	if (!best->button || distance < best->distance)
	{
		best->button = trace.ent;
		best->distance = distance;
	}
}

static edict_t *SV_VRContactTraceButton (edict_t *player,
	const vr_weapon_contact_t *previous, const vr_weapon_contact_t *current,
	int hand)
{
	vec3_t eye, grip, start, end, shaft;
	trace_t reach;
	sv_vr_contact_button_hit_t best;
	int axis, point, steps;

	memset (&best, 0, sizeof (best));
	best.distance = FLT_MAX;
	VectorAdd (player->v.origin, player->v.view_ofs, eye);
	VectorAdd (player->v.origin, current->grip[hand], grip);
	/* A blocked eye-to-grip point trace vetoes the hand for this sample. */
	reach = SV_Move (eye, vec3_origin, vec3_origin, grip,
		MOVE_NOMONSTERS, player);
	if (reach.startsolid || reach.allsolid || reach.fraction < 1.0f)
		return NULL;

	/* The shaft can cross a small button while its endpoints miss. Use
	 * bounded point sweeps along it, in the current body frame so ordinary
	 * locomotion does not become weapon motion. The current shaft is a
	 * stationary-contact fallback, matching the donor's point-trace policy. */
	VectorSubtract (current->tip[hand], current->base[hand], shaft);
	steps = CLAMP (1, (int)ceilf (VectorLength (shaft) / 3.0f), 32);
	for (point = -1; point <= steps; point++)
	{
		const float t = point < 0 ? 0.0f : (float)point / steps;
		for (axis = 0; axis < 3; axis++)
		{
			start[axis] = player->v.origin[axis] + (point < 0 ?
				current->base[hand][axis] :
				previous->base[hand][axis] + t *
				(previous->tip[hand][axis] - previous->base[hand][axis]));
			end[axis] = player->v.origin[axis] + (point < 0 ?
				current->tip[hand][axis] :
				current->base[hand][axis] + t * shaft[axis]);
		}
		SV_VRContactConsiderButtonTrace (player, grip, start, end, &best);
	}
	return best.button;
}

static void SV_VRContactRemember (client_t *client, edict_t *ent,
	const vr_weapon_contact_t *sample, double received_at)
{
	client->private_vr_contact_previous = *sample;
	VectorCopy (ent->v.origin, client->private_vr_contact_body_origin);
	client->private_vr_contact_previous_received = received_at;
	client->private_vr_contact_previous_valid = true;
}

/* Returns false only when a callback invalidated the player owner. */
static qboolean SV_VRContactProcessCommand (client_t *client, edict_t *ent,
	const usercmd_t *queued_command)
{
	usercmd_t command = *queued_command;
	vr_weapon_contact_t sample = command.vr_contact;
	vr_weapon_contact_t previous;
	vec3_t body_origin;
	int hand;

	if (client->private_vr_contact_cursor_valid &&
		(int)command.sequence <= client->private_vr_contact_last_sequence)
		return true;
	if (client->private_vr_contact_cursor_valid &&
		(int)command.sequence != client->private_vr_contact_last_sequence + 1)
		SV_ResetPrivateVRContactContinuity (client);
	client->private_vr_contact_last_sequence = (int)command.sequence;
	client->private_vr_contact_cursor_valid = true;

	if ((int)command.sequence <= client->private_discarded_move ||
		!SV_VRContactCommandValid (client, ent, &command))
	{
		SV_ResetPrivateVRContactContinuity (client);
		return true;
	}

	if (!client->private_vr_contact_previous_valid)
	{
		SV_VRContactRemember (client, ent, &sample,
			command.vr_contact_received);
		return true;
	}
	previous = client->private_vr_contact_previous;
	VectorCopy (client->private_vr_contact_body_origin, body_origin);
	if (!SV_VRContactTransitionValid (client, ent, &command, &previous,
		client->private_vr_contact_previous_received, body_origin))
	{
		SV_ResetPrivateVRContactContinuity (client);
		SV_VRContactRemember (client, ent, &sample,
			command.vr_contact_received);
		return true;
	}

	if (!SV_VRWeaponCollisionEnabled ())
		memset (client->private_vr_contact_button, 0,
			sizeof (client->private_vr_contact_button));
	for (hand = 0; SV_VRWeaponCollisionEnabled () && hand < 2; hand++)
	{
		edict_t *button;
		int button_number;
		int touch_index;
		int saved_self, saved_other;
		float saved_time;
		vec3_t callback_origin;

		if (!(sample.flags & (1u << hand)))
		{
			client->private_vr_contact_button[hand] = 0;
			continue;
		}
		button = SV_VRContactTraceButton (ent, &previous, &sample, hand);
		button_number = button ? NUM_FOR_EDICT (button) : 0;
		if (button_number <= 0 || button_number >= qcvm->num_edicts)
			button_number = 0;
		if (!button_number || button_number == client->private_vr_contact_button[hand])
		{
			client->private_vr_contact_button[hand] = button_number;
			continue;
		}
		if (!SV_VRContactButtonTouchAllowed (button, &touch_index))
		{
			client->private_vr_contact_button[hand] = 0;
			continue;
		}

		/* The queued pose is a local copy before QC can mutate client state. */
		ED_Retain (button);
		client->private_vr_contact_button[hand] = button_number;
		VectorCopy (ent->v.origin, callback_origin);
		saved_self = pr_global_struct->self;
		saved_other = pr_global_struct->other;
		saved_time = pr_global_struct->time;
		pr_global_struct->self = EDICT_TO_PROG (button);
		pr_global_struct->other = EDICT_TO_PROG (ent);
		pr_global_struct->time = qcvm->time;
		PR_ExecuteProgram (touch_index);
		pr_global_struct->self = saved_self;
		pr_global_struct->other = saved_other;
		pr_global_struct->time = saved_time;
		ED_Release (button);

		if (!SV_VRContactOwnerLive (client, ent))
		{
			SV_GorillaInvalidateAccepted (client);
			SV_VRContactInvalidateAccepted (client);
			/* Death is a valid QC outcome: let the frame's ordinary death
			 * observer run when the player entity and connection still exist. */
			return client->active && client->spawned &&
				client->edict == ent && !ent->free;
		}
		if (!client->private_vr_contact_cursor_valid)
			return false; /* serverinfo/reset began during the callback */
		if (client->private_vr_contact_last_sequence != (int)command.sequence)
			return true; /* relocation consumed all previously accepted contacts */
		if (!SV_VRContactSampleValid (client, ent, &command) ||
			SV_VRContactDistance (ent->v.origin, callback_origin) > 0.01f)
		{
			if (SV_VRContactDistance (ent->v.origin, callback_origin) > 0.01f)
				SV_GorillaInvalidateAccepted (client);
			SV_VRContactInvalidateAccepted (client);
			return true;
		}
		SV_VRContactFeedback (client, hand);
	}

	if (sample.flags & VR_WEAPON_CONTACT_IMMERSIVE_MELEE)
	{
		vec3_t callback_origin;
		qboolean dwell_pair = SV_VRDwellBerserkPairSelected (client, ent,
			&command);
		int direct_subtype = SV_VR_DIRECT_MELEE_NONE;
		qboolean direct_melee = SV_VRDirectMeleeContactSelected (client,
			ent, &command, &direct_subtype);
		qboolean selected_axe = SV_VRStockAxeContactProfile () !=
			VR_WEAPON_CONTACT_PROFILE_NONE &&
			SV_VRStockAxeSelected (client, ent, &command, NULL);
		qcvm_t *dwell_vm = qcvm;
		dprograms_t *dwell_progs = qcvm->progs;
		edict_t *melee_edicts = qcvm->edicts;
		globalvars_t *dwell_globals = pr_global_struct;
		float *dwell_vm_globals = qcvm->globals;
		VectorCopy (ent->v.origin, callback_origin);
		for (hand = 0; hand < 2; hand++)
			if (sample.flags & (1u << hand))
			{
				qboolean callback_entered = SV_VRContactProcessMelee (client,
					ent, &command, &previous, hand);
				/* The first hand may run side-effecting QC even when its
				 * outcome returns false. Never let the second hand borrow a
				 * changed VM, player, origin or contact cursor. */
				if ((dwell_pair || direct_melee || selected_axe) && callback_entered)
				{
					/* A reset or VM replacement already retired this cursor.
					 * Do not revive it through relocation invalidation. */
					if (qcvm != dwell_vm || qcvm->progs != dwell_progs ||
						qcvm->edicts != melee_edicts ||
						qcvm->globals != dwell_vm_globals ||
						pr_global_struct != dwell_globals ||
						!(dwell_pair ?
							SV_DwellBerserkAkimboProgramLoaded () :
							selected_axe ?
							SV_VRStockAxeContactProfile () != VR_WEAPON_CONTACT_PROFILE_NONE :
							(direct_subtype == SV_VR_DIRECT_MELEE_ENYO_SWORD ?
								SV_EnyoMeleeProgramLoaded () : SV_QBJ3MeleeProgramLoaded ())) ||
						!client->private_vr_contact_cursor_valid)
						return false;
					if (client->private_vr_contact_last_sequence !=
						(int)command.sequence)
						return true;
					if (!client->active || !client->spawned ||
						client->edict != ent || ent->free)
						return false;
					if ((dwell_pair &&
							!SV_VRDwellPhysicalOutcomeContextValid (client,
								ent, dwell_vm, dwell_progs, dwell_globals,
								dwell_vm_globals, callback_origin, true,
								(int)command.sequence)) ||
						(direct_melee &&
							(!SV_VRDirectMeleeOutcomeContextValid (client, ent,
								dwell_vm, dwell_progs, melee_edicts, dwell_globals,
								dwell_vm_globals, callback_origin, true,
								(int)command.sequence, direct_subtype, false) ||
							 !client->private_vr_contact_previous_valid)) ||
						(selected_axe &&
							SV_VRContactDistance (ent->v.origin, callback_origin) > 0.01f) ||
						!(dwell_pair ?
							SV_VRDwellBerserkPairSelected (client, ent,
								&command) : selected_axe ?
							SV_VRStockAxeSelected (client, ent, &command, NULL) :
							SV_VRDirectMeleeContactSelected (client, ent,
								&command, NULL)) ||
						!SV_VRContactSampleValid (client, ent, &command))
					{
						SV_GorillaInvalidateAccepted (client);
						SV_VRContactInvalidateAccepted (client);
						return true;
					}
				}
			}
		if (!SV_VRContactOwnerLive (client, ent))
		{
			SV_GorillaInvalidateAccepted (client);
			SV_VRContactInvalidateAccepted (client);
			return client->active && client->spawned &&
				client->edict == ent && !ent->free;
		}
		if (!client->private_vr_contact_cursor_valid)
			return false;
		if (client->private_vr_contact_last_sequence != (int)command.sequence)
			return true;
		if (!SV_VRContactSampleValid (client, ent, &command) ||
			SV_VRContactDistance (ent->v.origin, callback_origin) > 0.01f)
		{
			if (SV_VRContactDistance (ent->v.origin, callback_origin) > 0.01f)
				SV_GorillaInvalidateAccepted (client);
			SV_VRContactInvalidateAccepted (client);
			return true;
		}
	}
	else
	{
		memset (client->private_vr_melee_arc, 0,
			sizeof (client->private_vr_melee_arc));
		memset (client->private_vr_melee_peak_speed, 0,
			sizeof (client->private_vr_melee_peak_speed));
		memset (client->private_vr_melee_consumed, 0,
			sizeof (client->private_vr_melee_consumed));
		memset (client->private_vr_melee_stroke_direction, 0,
			sizeof (client->private_vr_melee_stroke_direction));
		for (hand = 0; hand < 2; hand++)
			SV_ResetPrivateVRDirectMeleeStroke (client, hand);
	}

	if (!SV_VRContactSampleValid (client, ent, &command))
		SV_ResetPrivateVRContactContinuity (client);
	else
		SV_VRContactRemember (client, ent, &sample,
			command.vr_contact_received);
	return true;
}

static void SV_VRContactObserveSpawn (client_t *client)
{
	if (!client->spawned)
	{
		SV_GorillaInvalidateAccepted (client);
		SV_ResetPrivateVRContactContinuity (client);
		client->private_vr_contact_spawn_seen = false;
		return;
	}
	if (!client->edict || client->edict->free ||
		!isfinite (client->edict->v.health) || client->edict->v.health <= 0 ||
		client->edict->v.deadflag)
	{
		/* A death observed at the frame boundary breaks pose continuity before
		 * QC can respawn this player during the same command. */
		SV_GorillaInvalidateAccepted (client);
		SV_ResetPrivateVRContactContinuity (client);
		return;
	}
	if (!client->private_vr_contact_spawn_seen)
	{
		SV_ResetPrivateVRContactContinuity (client);
		client->private_vr_contact_cursor_valid = false;
		client->private_vr_contact_last_sequence = 0;
		client->private_vr_contact_spawn_seen = true;
	}
}

static qboolean SV_VRContactDrainQueued (edict_t *ent, client_t *client,
	int completed_move)
{
	unsigned int head, count, offset;

	if (client->private_cmd_queue_count > SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_head >= SV_PRIVATE_CMD_QUEUE_SIZE)
	{
		SV_ResetPrivateVRContactContinuity (client);
		client->private_vr_contact_cursor_valid = false;
		return true;
	}
	if (!client->private_vr_contact_cursor_valid ||
		client->private_discarded_move > client->private_vr_contact_last_sequence ||
		client->private_retired_move > client->private_vr_contact_last_sequence)
	{
		int cutoff = q_max (client->private_discarded_move,
			client->private_retired_move);
		SV_ResetPrivateVRContactContinuity (client);
		if (cutoff > 0)
		{
			client->private_vr_contact_last_sequence = cutoff;
			client->private_vr_contact_cursor_valid = true;
		}
	}

	head = client->private_cmd_queue_head;
	count = client->private_cmd_queue_count;
	for (offset = 0; offset < count; offset++)
	{
		usercmd_t command = client->private_cmd_queue[(head + offset) %
			SV_PRIVATE_CMD_QUEUE_SIZE];
		if ((int)command.sequence > completed_move)
			break;
		if (client->private_vr_contact_cursor_valid &&
			(int)command.sequence <= client->private_vr_contact_last_sequence)
			continue;
		if (!SV_VRContactProcessCommand (client, ent, &command))
			return false;
		if (client->private_cmd_queue_head != head ||
			client->private_cmd_queue_count != count)
			return false;
	}
	return true;
}

static qboolean SV_RunPrivateVRWeaponThink (edict_t *ent, client_t *client,
	const usercmd_t *pose_cmd)
{
	sv_vr_weapon_pose_scope_t scope;
	qboolean alive;
	if (ent->v.nextthink <= 0 || ent->v.nextthink > qcvm->time + host_frametime)
		return true;
	SV_BeginPrivateVRWeaponPose (ent, client, pose_cmd, &scope);
	alive = SV_RunThink (ent);
	SV_EndPrivateVRWeaponPose (ent, &scope);
	return alive;
}

/* The private trial is deliberately narrower than the ordinary client owner:
 * stock hull, WALK (including water), and no moving-pusher authority. */
static qboolean SV_PrivateWalkTrialStockHull (edict_t *ent)
{
	vec3_t mins = {-16, -16, -24};
	vec3_t maxs = {16, 16, 32};
	return ent->v.solid == SOLID_SLIDEBOX &&
		VectorCompare (ent->v.mins, mins) && VectorCompare (ent->v.maxs, maxs);
}

/* Death is a normal client-physics transition, not a malformed private
 * command. Keep queue ownership until a native frame consumes the input. */
qboolean SV_PrivateWalkTrialTerminalState (client_t *client)
{
	edict_t *ent;
	if (!client || !client->active || !client->spawned ||
		!client->edict || client->edict->free)
		return false;
	ent = client->edict;
	if (!isfinite (ent->v.health) || !isfinite (ent->v.deadflag) ||
		!isfinite (ent->v.movetype) ||
		(ent->v.health > 0 && ent->v.deadflag == DEAD_NO))
		return false;
	return ent->v.movetype == MOVETYPE_NONE || ent->v.movetype == MOVETYPE_WALK ||
		ent->v.movetype == MOVETYPE_FLY || ent->v.movetype == MOVETYPE_NOCLIP ||
		ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_BOUNCE ||
		ent->v.movetype == MOVETYPE_GIB;
}

/* Classification observes the current owner, not the previous frame's
 * dispatcher. Callers still choose which states their execution permits. */
sv_private_move_state_t SV_PrivateWalkTrialClassifyState (client_t *client)
{
	edict_t *ent;
	if (!client || !client->active || !client->spawned ||
		!client->edict || client->edict->free)
		return SV_PRIVATE_MOVE_REJECTED;
	ent = client->edict;
	if (!isfinite (ent->v.health) || !isfinite (ent->v.deadflag) ||
		!isfinite (ent->v.movetype))
		return SV_PRIVATE_MOVE_REJECTED;
	if (SV_PrivateWalkTrialTerminalState (client))
		return SV_PRIVATE_MOVE_TERMINAL;
	if (!SV_PrivateWalkTrialStockHull (ent))
		return SV_PRIVATE_MOVE_REJECTED;
	if (ent->v.movetype == MOVETYPE_WALK)
		return SV_PRIVATE_MOVE_WALK;
	if (ent->v.movetype == MOVETYPE_NOCLIP || ent->v.movetype == MOVETYPE_FLY)
		return SV_PRIVATE_MOVE_NATIVE;
	return SV_PRIVATE_MOVE_REJECTED;
}

/* Receipt and snapshots admit qualified native states as well as WALK.
 * This validation must not run water categorization or consume input. */
const char *SV_PrivateWalkTrialFrameStateError (edict_t *ent, client_t *client,
	const usercmd_t *cmd)
{
	eval_t *customphysics;
	int groundprog, groundnum, i;
	edict_t *ground;
	sv_private_move_state_t state;

	if (qcvm != &sv.qcvm)
		return "server QC VM changed";
	if (!client || !ent || !client->active || !client->spawned ||
		client->edict != ent || ent->free)
		return "client owner is no longer live";
	if (client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!SV_PrivateWalkTrialSelected (client))
		return "private profile selection changed";
	state = SV_PrivateWalkTrialClassifyState (client);
	if (state == SV_PRIVATE_MOVE_REJECTED)
		return "owner left supported stock movement state";
	if (cmd && cmd->vr_gorilla_motion.flags)
		return "trusted Gorilla motion is outside the raw trial";
	if (cmd && cmd->vr_gorilla.flags &&
		!VRG_InputValid (&cmd->vr_gorilla))
		return "invalid raw Gorilla input";
	for (i = 0; i < 3; i++)
	{
		if (!isfinite (ent->v.origin[i]))
			return "owner has a non-finite origin";
		if (!isfinite (ent->v.velocity[i]))
			return "owner has a non-finite velocity";
	}
	/* Native tossed/gibbed corpses use point contents (-1/-2) outside water,
	 * while living selected movement requires the ordinary depth 0..3. */
	if (!isfinite (ent->v.waterlevel) ||
		ent->v.waterlevel < (state == SV_PRIVATE_MOVE_TERMINAL ? CONTENTS_SOLID : 0) ||
		ent->v.waterlevel > 3)
		return "invalid owner water level";
	if (state == SV_PRIVATE_MOVE_TERMINAL)
		return NULL; // dead hull/ground references are not a living WALK contract

	customphysics = GetEdictFieldValue (ent, qcvm->extfields.customphysics);
	if (customphysics && customphysics->function)
		return "customphysics became active";

	groundprog = ent->v.groundentity; // QC entity slots are integer byte offsets
	if (qcvm->edict_size <= 0 || groundprog < 0 || (groundprog &&
		(groundprog % qcvm->edict_size || groundprog / qcvm->edict_size >= qcvm->num_edicts)))
		return "invalid ground entity";
	if (groundprog)
	{
		groundnum = groundprog / qcvm->edict_size;
		ground = EDICT_NUM (groundnum);
		if (ground->free)
			return "stale ground entity";
		if (ground->v.movetype == MOVETYPE_PUSH && ground->v.solid == SOLID_BSP)
		{
			if (sv_gameplayfix_elevators.value < 3.f)
				return "owner is riding a pusher";
		}
	}
	return NULL;
}

/* A command already executing PMove must remain WALK after each callback.
 * Native eligibility does not authorize PM_NORMAL for a different movetype. */
const char *SV_PrivateWalkTrialStateError (edict_t *ent, client_t *client,
	const usercmd_t *cmd)
{
	const char *failure = SV_PrivateWalkTrialFrameStateError (ent, client, cmd);
	edict_t *ground;
	if (failure)
		return failure;
	if (sv.paused)
		return "server paused";
	if (SV_PrivateWalkTrialClassifyState (client) != SV_PRIVATE_MOVE_WALK)
		return "owner left stock WALK hull";
	SV_CheckWater (ent);
	if (!isfinite (ent->v.waterlevel) || ent->v.waterlevel < 0 || ent->v.waterlevel > 3)
		return "invalid owner water level";
	if (ent->v.groundentity)
	{
		ground = PROG_TO_EDICT (ent->v.groundentity);
		if (ground->v.movetype == MOVETYPE_PUSH && ground->v.solid == SOLID_BSP)
			client->private_pmove_pusher_interaction = true;
	}
	return NULL;
}

static qboolean SV_PrivateWalkTrialBuildBounds (edict_t *ent,
	const movevars_t *vars, float seconds, const usercmd_t *command,
	vec3_t bounds[2])
{
	float speed, reach, acceleration;
	int i;

	if (!isfinite (seconds) || seconds <= 0 || seconds > 0.1251f)
		return false;
	speed = 0;
	for (i = 0; i < 3; i++)
	{
		if (!isfinite (ent->v.origin[i]) || !isfinite (ent->v.velocity[i]) ||
			!isfinite (ent->v.mins[i]) || !isfinite (ent->v.maxs[i]))
			return false;
		speed = fmaxf (speed, fabsf (ent->v.velocity[i]));
	}
	acceleration = fmaxf (fabsf (vars->accelerate), fabsf (vars->airaccelerate)) *
		fabsf (vars->maxspeed);
	reach = (speed + fabsf (vars->maxspeed)) * seconds +
		0.5f * (fabsf (vars->gravity * vars->entgravity) + acceleration) * seconds * seconds +
	fabsf (vars->jumpspeed) * seconds + (float)vars->stepheight + 16.0f;
	if (!isfinite (reach))
		return false;
	for (i = 0; i < 3; i++)
	{
		bounds[0][i] = ent->v.origin[i] + ent->v.mins[i] - reach;
		bounds[1][i] = ent->v.origin[i] + ent->v.maxs[i] + reach;
		if (command && command->vr_gorilla.flags)
		{
			/* PMove traces the head-to-palm reach as well as the body.
			 * Include those command-owned endpoints, without widening every
			 * ordinary desktop trial command. */
			float extra = fmaxf (64.0f, VR_GORILLA_MAX_HAND_SPEED * seconds) +
				VR_GORILLA_RADIUS;
			float low = fminf (command->vr_gorilla.head[i],
				fminf (command->vr_gorilla.hand[0][i],
					command->vr_gorilla.hand[1][i]));
			float high = fmaxf (command->vr_gorilla.head[i],
				fmaxf (command->vr_gorilla.hand[0][i],
					command->vr_gorilla.hand[1][i]));
			bounds[0][i] = fminf (bounds[0][i],
				ent->v.origin[i] + low - reach - extra);
			bounds[1][i] = fmaxf (bounds[1][i],
				ent->v.origin[i] + high + reach + extra);
		}
		if (!isfinite (bounds[0][i]) || !isfinite (bounds[1][i]))
			return false;
	}
	return true;
}

static qboolean SV_PrivateWalkTrialCollect (edict_t *ent,
	const movevars_t *vars, float seconds, const usercmd_t *command,
	vec3_t bounds[2])
{
	int i;

	if (!SV_PrivateWalkTrialBuildBounds (ent, vars, seconds, command, bounds) ||
		!SV_CollectPMovePhysents (ent, bounds))
		return false;
	/* The bounds intentionally cover the maximum reachable command sweep, not
	 * just current overlap. A pusher anywhere in that envelope stays under the
	 * legacy owner until moving-pusher contact parity is qualified. */
	for (i = 1; i < pmove.numphysent; i++)
	{
		int number = pmove.physents[i].info;
		edict_t *other;
		if (number <= 0 || number >= qcvm->num_edicts)
			return false;
		other = EDICT_NUM (number);
		if (other->free)
			return false;
	}
	return true;
}

static void SV_PrivateWalkTrialDrop (client_t *client, const char *reason)
{
	client_t *saved_host_client = host_client;
	edict_t *saved_sv_player = sv_player;
	Sys_Printf ("%s: private WALK trial disconnected: %s\n",
		client->name[0] ? client->name : "client", reason);
	if (client->active)
	{
		host_client = client;
		sv_player = client->edict;
		SV_DropClient (false);
		host_client = saved_host_client;
		sv_player = saved_sv_player;
	}
}

/* The pinned stock PreThink owns water sounds, damage and flags. Its velocity
 * drag and ledge-jump impulse overlap the selected PMove owner. Adapt the
 * inherited SV_FilterLegacyPMoveQCVelocityDelta at this command boundary:
 * remove only those stock velocity edits, preserving any residual QC force.
 * A zeroed velocity may be an intentional teleport pause and stays zero. */
static void SV_PrivateWalkTrialReconcileQCWater (edict_t *ent,
	const vec3_t before, int before_flags, int before_waterlevel,
	float before_health, float before_teleport_time, float seconds)
{
	vec3_t delta, stock_drag;
	const int after_flags = (int)ent->v.flags;
	const float after_teleport_time = ent->v.teleport_time;
	const qboolean qc_waterjump = (after_flags & FL_WATERJUMP) &&
		(before_waterlevel == 2) &&
		(!(before_flags & FL_WATERJUMP) ||
		 after_teleport_time > before_teleport_time ||
		 fabsf (ent->v.velocity[2] - 225.0f) < MOVE_EPSILON);

	if (VectorCompare (ent->v.velocity, vec3_origin))
		return;
	VectorSubtract (ent->v.velocity, before, delta);
	VectorClear (stock_drag);
	if (before_waterlevel > 0 && !(before_flags & FL_WATERJUMP) &&
		before_health >= 0)
	{
		VectorScale (before, -0.8f * before_waterlevel * seconds, stock_drag);
		VectorSubtract (delta, stock_drag, delta);
	}
	if (before_waterlevel >= 2 && ent->v.button2 &&
		!(after_flags & FL_WATERJUMP) && before_health > 0)
	{
		/* Stock PlayerJump overwrites z after WaterMove's drag. Removing drag
		 * from that component would invent a force. PMove owns the same swim
		 * assignment later; retain only a residual above the known QC write. */
		float swim_speed = ent->v.watertype == CONTENTS_WATER ? 100.0f :
			ent->v.watertype == CONTENTS_SLIME ? 80.0f : 50.0f;
		delta[2] = ent->v.velocity[2] - swim_speed;
	}
	else if (qc_waterjump)
		delta[2] -= 225.0f - (before[2] + stock_drag[2]);
	VectorAdd (before, delta, ent->v.velocity);
}

typedef enum
{
	SV_CLIENT_NATIVE_FRESH,
	SV_CLIENT_NATIVE_AFTER_PRETHINK,
	SV_CLIENT_NATIVE_AFTER_WEAPON_THINK
} sv_client_native_start_t;

/* One scheduling opportunity for the selected client's entire world pass.
 * Command batching must not repeatedly reopen the same Think deadline. */
typedef struct
{
	qboolean available;
	double world_frametime;
	float world_qc_frametime;
} sv_client_think_window_t;

static qboolean SV_TakeClientThinkWindow (sv_client_think_window_t *window)
{
	if (!window)
		return true; // ordinary native frame scheduling
	if (!window->available)
		return false;
	window->available = false;
	return true;
}

static qboolean SV_RunClientWeaponThink (edict_t *ent, client_t *client,
	const usercmd_t *command, sv_client_think_window_t *window)
{
	double saved_host_frametime = host_frametime;
	float saved_qc_frametime = pr_global_struct->frametime;
	qboolean alive;

	if (!window)
		return SV_RunPrivateVRWeaponThink (ent, client, command);
	if (!SV_TakeClientThinkWindow (window))
		return !ent->free;
	host_frametime = window->world_frametime;
	pr_global_struct->frametime = window->world_qc_frametime;
	alive = SV_RunPrivateVRWeaponThink (ent, client, command);
	host_frametime = saved_host_frametime;
	pr_global_struct->frametime = saved_qc_frametime;
	return alive;
}

static qboolean SV_Physics_ClientNativeFromPhase (edict_t *ent, int num,
	int completed_move, sv_client_native_start_t start,
	qboolean prior_weapon_think_ran, sv_client_think_window_t *think_window);

static qboolean SV_PrivateWalkTrialContinueTerminal (edict_t *ent,
	client_t *client, const usercmd_t *command, sv_client_native_start_t start,
	double world_frametime, float world_qc_frametime,
	sv_client_think_window_t *think_window)
{
	/* The callback that killed the owner has already run. Consume its dead
	 * contact sample, then let only the remaining native frame phases run.
	 * The selected completion tail, not this continuation, commits its ACK. */
	if (!SV_VRContactDrainQueued (ent, client, (int)command->sequence))
		return false;
	SV_ResetGorillaClient (client);
	client->vr_gorilla_last_sequence = (int)command->sequence;
	client->vr_gorilla_cursor_valid = true;
	client->private_move_native_frame = true;
	host_frametime = world_frametime;
	pr_global_struct->frametime = world_qc_frametime;
	return SV_Physics_ClientNativeFromPhase (ent, NUM_FOR_EDICT (ent),
		client->private_completed_move, start,
		start == SV_CLIENT_NATIVE_AFTER_WEAPON_THINK, think_window);
}

/* This owner runs only for explicitly selected private peers. Queue retirement
 * remains in SV_FinishPrivateUsercmds, after this function reports completion. */
static qboolean SV_Physics_ClientPrivateWalkTrial (edict_t *ent, client_t *client,
	unsigned queue_offset, qboolean accrue_credit,
	sv_client_think_window_t *think_window)
{
	playermove_t saved_pmove = pmove;
	movevars_t saved_movevars = movevars, trial_movevars;
	client_t *saved_host_client = host_client;
	edict_t *saved_sv_player = sv_player;
	usercmd_t command, ownership_command, *queued = NULL;
	double saved_host_frametime = host_frametime;
	float saved_qc_frametime = pr_global_struct->frametime;
	vec3_t bounds[2], prethink_velocity, preweapon_velocity;
	float seconds, prethink_health, prethink_teleport_time, postthink_teleport_time;
	float premove_teleport_time;
	int prethink_flags, prethink_groundentity, prethink_waterlevel;
	qboolean qc_waterjump_started;
	qboolean run_command = false, was_grounded = false, weapon_alive;
	qboolean q30_program = false;
	qboolean instant_stop_enabled = false;
	qboolean friendly_fire_scope;
	qboolean command_completed = false, suppress_trigger = false;
	qboolean terminal_completed = false;
	qboolean terminal_native_completed = false;
	const char *failure = NULL;
	float result_jump_secs = 0, result_waterjump_secs = 0;
	vr_gorilla_state_t result_gorilla;
	vec3_t result_gorilla_origin;
	unsigned int gorilla_reset_generation;
	qboolean gorilla_command_cutoff = false;
	int i;

	ED_Retain (ent);
	host_client = client;
	sv_player = ent;
	client->private_move_native_frame = false;
	if (!client->private_pmove_last_cmd_valid &&
		client->private_input_phase == PRIVATE_INPUT_RUNNING)
	{
		client->private_pmove_jump_secs = 0.0f;
		client->private_pmove_waterjump_secs = 0.0f;
	}

	if (!isfinite (client->private_pmove_credit_msec) ||
		client->private_pmove_credit_msec < 0.0)
	{
		failure = "invalid command-time credit";
		goto cleanup;
	}
	if (!isfinite (host_frametime) || host_frametime < 0.0)
	{
		failure = "invalid host frame time";
		goto cleanup;
	}
	if (accrue_credit && !sv.paused)
		client->private_pmove_credit_msec = fmin (250.0,
			client->private_pmove_credit_msec + host_frametime * 1000.0);
	if (client->private_pmove_credit_msec > 250.0)
		client->private_pmove_credit_msec = 250.0;

	if (client->private_cmd_queue_count > SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_head >= SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_msec > SV_PRIVATE_CMD_QUEUE_MAX_MSEC)
	{
		failure = "invalid accepted-command queue state";
		goto cleanup;
	}
	if (queue_offset < client->private_cmd_queue_count)
		queued = &client->private_cmd_queue[(client->private_cmd_queue_head +
			queue_offset) % SV_PRIVATE_CMD_QUEUE_SIZE];
	if (queued && (queued->msec < 1 || queued->msec > 125 ||
		(int)queued->sequence <= client->private_completed_move))
	{
		failure = "invalid accepted queue head";
		goto cleanup;
	}
	run_command = queued && client->private_pmove_credit_msec >= queued->msec;
	seconds = queued ? queued->msec * 0.001f : 0.125f;
	if (queued)
		command = *queued;
	else if (client->private_pmove_last_cmd_valid)
		command = client->private_pmove_last_cmd;
	else
	{
		memset (&command, 0, sizeof (command));
		VectorCopy (ent->v.v_angle, command.viewangles);
	}
	if (!client->vr_gorilla_capable || !sv_gorilla.value)
		SV_ResetGorillaClient (client);
	if (run_command && client->vr_gorilla_cursor_valid &&
		(int)command.sequence > client->vr_gorilla_last_sequence + 1)
		SV_ResetGorillaClient (client);
	if (run_command && client->vr_gorilla_cursor_valid &&
		(int)command.sequence <= client->vr_gorilla_last_sequence)
		gorilla_command_cutoff = true;
	gorilla_reset_generation = client->vr_gorilla_reset_generation;
	if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	if (run_command && (!PMSV_BuildMoveVars (&trial_movevars, ent, sv.protocolflags) ||
		!SV_PrivateWalkTrialCollect (ent, &trial_movevars, seconds,
			client->vr_gorilla_capable && sv_gorilla.value ?
				&command : NULL, bounds)))
	{
		failure = "PMove preflight failed";
		goto cleanup;
	}

	if (!run_command)
	{
		/* Maintenance runs only before any command has completed this frame. */
		if (queue_offset != 0)
			goto cleanup;
		/* Keep only the last completed levels and pose during a zero-time QC
		 * maintenance pass. An uncompleted queue head never leaks into callbacks. */
		if (client->private_pmove_last_cmd_valid)
			command = client->private_pmove_last_cmd;
		else
		{
			memset (&command, 0, sizeof (command));
			VectorCopy (ent->v.v_angle, command.viewangles);
		}
		ownership_command = command;
		suppress_trigger = SV_VRMeleeSuppressNativeTrigger (client, ent,
			&ownership_command);
		command.impulse = 0;
		command.seconds = 0;
		command.msec = 0;
		VectorClear (command.vr_roomscalemove);
		client->cmd = command;
		VectorCopy (command.viewangles, ent->v.v_angle);
		ent->v.button0 = !suppress_trigger &&
			(command.buttons & BUTTON_ATTACK) != 0;
		ent->v.button2 = (command.buttons & 2) != 0;
		SV_SetClientExtraButtons (ent, command.buttons);
		ent->v.impulse = 0;
		host_frametime = 0;
		pr_global_struct->frametime = 0;
		SV_ClientUpdateAnglesForClient (client);
		pr_global_struct->time = qcvm->time;
		pr_global_struct->self = EDICT_TO_PROG (ent);
		SV_CoopRespawnRefreshClientInventory (ent);
		PR_ExecuteProgram (pr_global_struct->PlayerPreThink);
		if (!client->active || ent->free)
		{
			failure = "player removed during maintenance PreThink";
			goto cleanup;
		}
		if (SV_PrivateWalkTrialTerminalState (client))
		{
			if (!SV_PrivateWalkTrialContinueTerminal (ent, client, &command,
				SV_CLIENT_NATIVE_AFTER_PRETHINK, saved_host_frametime,
				saved_qc_frametime, think_window))
				failure = "terminal maintenance PreThink continuation failed";
			goto cleanup;
		}
		if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
			goto cleanup;
		/* Scheduled Think follows the host clock even without a move command. */
		SV_VRMeleeRefreshTriggerSuppression (client, ent,
			&ownership_command, &suppress_trigger);
		if (!SV_RunClientWeaponThink (ent, client, &ownership_command,
			think_window))
		{
			failure = "player removed during maintenance weapon Think";
			goto cleanup;
		}
		SV_VRMeleeRefreshTriggerSuppression (client, ent,
			&ownership_command, &suppress_trigger);
		host_frametime = 0;
		pr_global_struct->frametime = 0;
		if (SV_PrivateWalkTrialTerminalState (client))
		{
			if (!SV_PrivateWalkTrialContinueTerminal (ent, client, &command,
				SV_CLIENT_NATIVE_AFTER_WEAPON_THINK, saved_host_frametime,
				saved_qc_frametime, think_window))
				failure = "terminal maintenance weapon Think continuation failed";
			goto cleanup;
		}
		if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
			goto cleanup;
		SV_LinkEdict (ent, true);
		if (!client->active || ent->free)
		{
			failure = "player removed during maintenance trigger callbacks";
			goto cleanup;
		}
		pr_global_struct->time = qcvm->time;
		pr_global_struct->frametime = 0;
		pr_global_struct->self = EDICT_TO_PROG (ent);
		{
			sv_vr_weapon_pose_scope_t weapon_scope;
			SV_BeginPrivateVRWeaponPose (ent, client,
				&ownership_command, &weapon_scope);
			SV_VRMeleeRefreshTriggerSuppression (client, ent,
				&ownership_command, &suppress_trigger);
			friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
			PR_ExecuteProgram (pr_global_struct->PlayerPostThink);
			if (friendly_fire_scope)
				SV_CoopFriendlyFireEnd ();
			SV_EndPrivateVRWeaponPose (ent, &weapon_scope);
		}
		if (!client->active || ent->free)
		{
			failure = "player removed during maintenance PostThink";
			goto cleanup;
		}
		SV_CoopSharedObserveClientDeath (ent, NUM_FOR_EDICT (ent));
		if (client->spawned && client->edict == ent)
			SV_CoopRespawnRefreshClientInventory (ent);
		ent->v.impulse = 0;
		client->cmd.impulse = 0;
		goto cleanup;
	}

	/* Suppress only this accepted queue head. Keep original buttons intact for
	 * retirement and movement semantics. */
	suppress_trigger = SV_VRMeleeSuppressNativeTrigger (client, ent,
		&command);
	ownership_command = command;
	/* A complete head is staged exactly once; room-scale is consumed before QC
	 * and the shared PMove command below cannot apply it a second time. */
	command.seconds = seconds;
	client->cmd = command;
	VectorCopy (command.viewangles, ent->v.v_angle);
	ent->v.button0 = !suppress_trigger &&
		(command.buttons & BUTTON_ATTACK) != 0;
	ent->v.button2 = (command.buttons & 2) != 0;
	SV_SetClientExtraButtons (ent, command.buttons);
	ent->v.impulse = command.impulse;
	host_frametime = seconds;
	pr_global_struct->frametime = seconds;
	SV_ClientUpdateAnglesForClient (client);
	SV_ApplyPrivateRoomScaleMove (ent, client);
	SV_CheckWater (ent);
	instant_stop_enabled = SV_ClientInstantStopEnabled (client);
	SV_PrivateInstantStopBeforeQC (ent, client, &command,
		instant_stop_enabled, trial_movevars.pground);

	VectorCopy (ent->v.velocity, prethink_velocity);
	prethink_flags = (int)ent->v.flags;
	prethink_groundentity = (int)ent->v.groundentity;
	prethink_waterlevel = (int)ent->v.waterlevel;
	prethink_health = ent->v.health;
	prethink_teleport_time = ent->v.teleport_time;
	was_grounded = (prethink_flags & FL_ONGROUND) != 0;
	q30_program = SV_PrivateWalkTrialQ30Program ();
	/* Exact q30 QuakeC owns both press and release. A short, low takeoff can
	 * still be inside the floor probe after the button is released. */
	pr_global_struct->time = qcvm->time;
	pr_global_struct->frametime = seconds;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	SV_CoopRespawnRefreshClientInventory (ent);
	SV_VRMeleeRefreshTriggerSuppression (client, ent,
		&ownership_command, &suppress_trigger);
	PR_ExecuteProgram (pr_global_struct->PlayerPreThink);
	if (!client->active || ent->free)
	{
		failure = "player removed during PreThink";
		goto cleanup;
	}
	if (SV_PrivateWalkTrialTerminalState (client))
	{
		if (!SV_PrivateWalkTrialContinueTerminal (ent, client, &command,
			SV_CLIENT_NATIVE_AFTER_PRETHINK, saved_host_frametime,
			saved_qc_frametime, think_window))
		{
			failure = "terminal PreThink continuation failed";
			goto cleanup;
		}
		if (client->private_pmove_credit_msec < command.msec)
		{
			failure = "command-time credit changed during callbacks";
			goto cleanup;
		}
		terminal_completed = terminal_native_completed = true;
		goto complete_terminal_command;
	}
	qc_waterjump_started = !(prethink_flags & FL_WATERJUMP) &&
		((int)ent->v.flags & FL_WATERJUMP);
	postthink_teleport_time = ent->v.teleport_time;
	/* Weapon Think must see native QC's velocity. PMove-only drag/jump
	 * reconciliation runs only if that callback survives and leaves velocity
	 * alone; a callback velocity write remains authoritative. */
	SV_CheckVelocity (ent);
	if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	/* Weapon Think is scheduled against the world frame, not the packet's
	 * duration; ordinary PMove below still consumes the complete command. */
	SV_VRMeleeRefreshTriggerSuppression (client, ent,
		&ownership_command, &suppress_trigger);
	VectorCopy (ent->v.velocity, preweapon_velocity);
	weapon_alive = SV_RunClientWeaponThink (ent, client, &client->cmd,
		think_window);
	host_frametime = seconds;
	pr_global_struct->frametime = seconds;
	if (!weapon_alive || !client->active || ent->free)
	{
		failure = "player removed during weapon Think";
		goto cleanup;
	}
	SV_VRMeleeRefreshTriggerSuppression (client, ent,
		&ownership_command, &suppress_trigger);
	if (SV_PrivateWalkTrialTerminalState (client))
	{
		if (!SV_PrivateWalkTrialContinueTerminal (ent, client, &command,
			SV_CLIENT_NATIVE_AFTER_WEAPON_THINK, saved_host_frametime,
			saved_qc_frametime, think_window))
		{
			failure = "terminal weapon Think continuation failed";
			goto cleanup;
		}
		if (client->private_pmove_credit_msec < command.msec)
		{
			failure = "command-time credit changed during callbacks";
			goto cleanup;
		}
		terminal_completed = terminal_native_completed = true;
		goto complete_terminal_command;
	}
	if (VectorCompare (ent->v.velocity, preweapon_velocity))
	{
		if (!q30_program &&
			(prethink_waterlevel > 0 || ((int)ent->v.flags & FL_WATERJUMP)))
			SV_PrivateWalkTrialReconcileQCWater (ent, prethink_velocity,
				prethink_flags, prethink_waterlevel, prethink_health,
				prethink_teleport_time, seconds);
		/* Stock QC owns jump sounds and flags; PMove owns its dry impulse.
		 * Exact q30 QuakeC owns its own impulse. Preserve a teleporter's
		 * deliberate pause at zero velocity. */
		if (!q30_program && prethink_waterlevel < 2 && was_grounded && (command.buttons & 2) &&
			ent->v.teleport_time <= qcvm->time &&
			(prethink_teleport_time <= qcvm->time ||
			 ent->v.teleport_time == prethink_teleport_time) &&
			!(VectorCompare (ent->v.velocity, vec3_origin) &&
			  !VectorCompare (prethink_velocity, vec3_origin)))
			VectorCopy (prethink_velocity, ent->v.velocity);
	}
	SV_CheckVelocity (ent);
	if ((failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	if (!PMSV_BuildMoveVars (&trial_movevars, ent, sv.protocolflags) ||
		!SV_PrivateWalkTrialCollect (ent, &trial_movevars, seconds,
			client->vr_gorilla_capable && sv_gorilla.value ?
				&command : NULL, bounds))
	{
		failure = "post-QC physent collection failed";
		goto cleanup;
	}
	if (instant_stop_enabled)
		trial_movevars.flags |= MOVEFLAG_VR_INSTANT_STOP;

	movevars = trial_movevars;
	pmove.pm_type = PM_NORMAL;
	/* SV_AirMove ignores backward input during the stock teleporter's
	 * teleport_time window. Waterjump is a separate native movement path. */
	pmove.block_teleport_backmove =
		!((int)ent->v.flags & FL_WATERJUMP) &&
		client->private_pmove_waterjump_secs == 0.0f &&
		qcvm->time < ent->v.teleport_time;
	pmove.cmd = client->cmd;
	pmove.vr_instant_stop_preapplied = true;
	/* Native SV_AirMove accelerates using ent->angles. QuakeC teleporters
	 * and respawn relocation hold that orientation with fixangle until the
	 * setangle snapshot; this command may still face the old way. */
	if (ent->v.fixangle)
		VectorCopy (ent->v.angles, pmove.cmd.viewangles);
	pmove.cmd.seconds = seconds;
	pmove.cmd.msec = command.msec;
	pmove.cmd.impulse = command.impulse;
	if (gorilla_command_cutoff || !client->vr_gorilla_capable ||
		!sv_gorilla.value)
		memset (&pmove.cmd.vr_gorilla, 0, sizeof (pmove.cmd.vr_gorilla));
	VectorClear (pmove.cmd.vr_roomscalemove);
	VectorCopy (ent->v.origin, pmove.origin);
	VectorCopy (ent->v.velocity, pmove.velocity);
	VectorCopy (pmove.cmd.viewangles, pmove.angles);
	VectorSet (pmove.gravitydir, 0, 0, -1);
	VectorCopy (ent->v.mins, pmove.player_mins);
	VectorCopy (ent->v.maxs, pmove.player_maxs);
	/* Stock QC may clear FL_JUMPRELEASED for a sound; PMove uses the prior
	 * state. Exact q30 QC owns both the impulse and its updated release latch. */
	pmove.jump_held = (((int)(q30_program ? ent->v.flags : prethink_flags) &
		FL_JUMPRELEASED) == 0);
	pmove.qc_jump_owner = q30_program;
	pmove.jump_secs = client->private_pmove_jump_secs;
	pmove.waterjumptime = client->private_pmove_waterjump_secs;
	pmove.waterlevel = 0;
	pmove.watertype = CONTENTBIT_EMPTY;
	pmove.onladder = false;
	pmove.safeorigin_known = false;
	pmove.gorilla_allowed = client->vr_gorilla_capable &&
		sv_gorilla.value != 0.0f && !gorilla_command_cutoff;
	pmove.gorilla_prepared = false;
	pmove.gorilla_authoring = false;
	pmove.gorilla = client->vr_gorilla_state;
	pmove.numtouch = 0;
	pmove.onground = false;
	pmove.groundent = 0;
	/* A scheduled QC Think can change this after PlayerPreThink. Do not
	 * restore an earlier deadline over that callback when PMove clears a
	 * waterjump timer. */
	premove_teleport_time = ent->v.teleport_time;
	if (was_grounded || ((int)ent->v.flags & FL_ONGROUND))
	{
		int groundprog = (int)ent->v.groundentity;
		if (!groundprog && was_grounded)
			groundprog = prethink_groundentity;
		if (!groundprog)
			pmove.onground = true; /* world */
		else if (groundprog > 0 && groundprog % qcvm->edict_size == 0)
		{
			int groundnum = groundprog / qcvm->edict_size;
			for (i = 0; i < pmove.numphysent; i++)
				if (pmove.physents[i].info == groundnum)
				{
					pmove.onground = true;
					pmove.groundent = i;
					break;
				}
		}
	}
	PM_PlayerMove (1.0f);
	result_gorilla = pmove.gorilla;
	VectorCopy (pmove.origin, result_gorilla_origin);
	/* Palm traces are not body touchindex entries. The stock trial cannot
	 * predict a moving brush under a planted hand, so enforce its existing
	 * pusher exclusion for both fresh and retained hand contacts. */
	for (i = 0; i < 2; i++)
	{
		int contacts[2] = {pmove.gorilla_contact[i], pmove.gorilla.surface[i]};
		int which;
		for (which = 0; which < 2; which++)
		{
			int number = contacts[which];
			edict_t *surface;
			if (number <= 0)
				continue;
			if (number >= qcvm->num_edicts)
			{
				failure = "Gorilla contact entity is outside the server world";
				goto cleanup;
			}
			surface = EDICT_NUM (number);
			if (surface->free ||
				(surface->v.movetype == MOVETYPE_PUSH &&
				 surface->v.solid == SOLID_BSP))
			{
				failure = "Gorilla palm contacted an unsupported moving pusher";
				goto cleanup;
			}
		}
	}
	result_jump_secs = pmove.jump_secs;
	result_waterjump_secs = pmove.waterjumptime;
	if (pmove.onground && (pmove.groundent < 0 || pmove.groundent >= pmove.numphysent))
	{
		failure = "PMove returned an invalid ground entity";
		goto cleanup;
	}
	if (pmove.onground)
	{
		int number = pmove.physents[pmove.groundent].info;
		if (number > 0 && number < qcvm->num_edicts)
		{
			edict_t *ground = EDICT_NUM (number);
			if (!ground->free && ground->v.movetype == MOVETYPE_PUSH &&
				ground->v.solid == SOLID_BSP)
				client->private_pmove_pusher_interaction = true;
		}
	}
	for (i = 0; i < pmove.numtouch; i++)
		if (pmove.touchindex[i] < 0 || pmove.touchindex[i] >= pmove.numphysent)
		{
			failure = "PMove returned an invalid solid touch";
			goto cleanup;
		}
	for (i = 0; i < pmove.numtouch; i++)
	{
		int number = pmove.physents[pmove.touchindex[i]].info;
		if (number > 0 && number < qcvm->num_edicts)
		{
			edict_t *other = EDICT_NUM (number);
			if (!other->free && other->v.movetype == MOVETYPE_PUSH &&
				other->v.solid == SOLID_BSP)
			{
				client->private_pmove_pusher_interaction = true;
				if (sv_gameplayfix_elevators.value < 3.f)
				{
					failure = "PMove contacted a moving pusher";
					goto cleanup;
				}
			}
		}
	}

	VectorCopy (pmove.origin, ent->v.origin);
	VectorCopy (pmove.velocity, ent->v.velocity);
	if (pmove.onground)
	{
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG (EDICT_NUM (pmove.physents[pmove.groundent].info));
	}
	else
	{
		ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
		ent->v.groundentity = 0;
	}
	if (pmove.jump_held)
		ent->v.flags = (int)ent->v.flags & ~FL_JUMPRELEASED;
	else
		ent->v.flags = (int)ent->v.flags | FL_JUMPRELEASED;
	if (result_waterjump_secs > 0.0f)
	{
		ent->v.flags = (int)ent->v.flags | FL_WATERJUMP;
		ent->v.teleport_time = qcvm->time + result_waterjump_secs;
	}
	else
	{
		ent->v.flags = (int)ent->v.flags & ~FL_WATERJUMP;
		if (premove_teleport_time == postthink_teleport_time)
			ent->v.teleport_time = (prethink_flags & FL_WATERJUMP) ? 0.0f :
				(qc_waterjump_started ? prethink_teleport_time : postthink_teleport_time);
		else
			ent->v.teleport_time = premove_teleport_time;
	}
	ent->v.waterlevel = pmove.waterlevel;
	ent->v.watertype = CONTENTS_EMPTY;
	if (pmove.watertype & CONTENTBIT_LAVA)
		ent->v.watertype = CONTENTS_LAVA;
	else if (pmove.watertype & CONTENTBIT_SLIME)
		ent->v.watertype = CONTENTS_SLIME;
	else if (pmove.watertype & CONTENTBIT_WATER)
		ent->v.watertype = CONTENTS_WATER;

	/* Link for QC contact queries, dispatch solid impacts, then let the normal
	 * trigger owner run once. SV_Impact callback velocity edits remain final. */
	SV_LinkEdict (ent, false);
	for (i = 0; i < pmove.numtouch && !ent->free; i++)
	{
		int number = pmove.physents[pmove.touchindex[i]].info;
		edict_t *other;
		if (number < 0 || number >= qcvm->num_edicts)
		{
			failure = "PMove touch entity is outside the server world";
			goto cleanup;
		}
		other = EDICT_NUM (number);
		if (!other->free && other != ent)
			SV_Impact (ent, other);
	}
	if (ent->free || !client->active)
	{
		failure = "player removed during solid impact callbacks";
		goto cleanup;
	}
	SV_LinkEdict (ent, true);
	if (!client->active || ent->free)
	{
		failure = "player removed during trigger callbacks";
		goto cleanup;
	}
	if (!SV_VRContactProcessCommand (client, ent, &command))
	{
		failure = "player invalidated during physical button callback";
		goto cleanup;
	}
	/* Movement, impacts, triggers and physical contacts have already run.
	 * A death here still receives this command's PostThink exactly once; the
	 * fresh native terminal frame starts on the next world tick. */
	if (!SV_PrivateWalkTrialTerminalState (client) &&
		(failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	pr_global_struct->time = qcvm->time;
	pr_global_struct->frametime = seconds;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	{
		sv_vr_weapon_pose_scope_t weapon_scope;
		SV_BeginPrivateVRWeaponPose (ent, client, &client->cmd,
			&weapon_scope);
		SV_VRMeleeRefreshTriggerSuppression (client, ent,
			&ownership_command, &suppress_trigger);
		friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
		PR_ExecuteProgram (pr_global_struct->PlayerPostThink);
		if (friendly_fire_scope)
			SV_CoopFriendlyFireEnd ();
		SV_EndPrivateVRWeaponPose (ent, &weapon_scope);
	}
	if (!client->active || ent->free)
	{
		failure = "player removed during PostThink";
		goto cleanup;
	}
	SV_CoopSharedObserveClientDeath (ent, NUM_FOR_EDICT (ent));
	terminal_completed = SV_PrivateWalkTrialTerminalState (client);
	if (!terminal_completed &&
		(failure = SV_PrivateWalkTrialStateError (ent, client, &command)) != NULL)
		goto cleanup;
	if (client->private_pmove_credit_msec < command.msec)
	{
		failure = "command-time credit changed during callbacks";
		goto cleanup;
	}
	/* A QC relocation, surface replacement or contact callback fences all
	 * hands accepted at the old origin. Never publish the pre-callback solver
	 * result as the baseline for a later replay command. */
	if (!terminal_native_completed && !gorilla_command_cutoff &&
		client->vr_gorilla_capable &&
		sv_gorilla.value &&
		client->vr_gorilla_reset_generation == gorilla_reset_generation)
	{
		vec3_t callback_delta;
		VectorSubtract (ent->v.origin, result_gorilla_origin, callback_delta);
		if (VectorLength (callback_delta) <= .01f)
		{
			client->vr_gorilla_state = result_gorilla;
			client->vr_gorilla_last_sequence = (int)command.sequence;
			client->vr_gorilla_cursor_valid = true;
		}
		else
			SV_GorillaInvalidateAccepted (client);
	}
complete_terminal_command:
	client->private_completed_move = (int)command.sequence;
	if (client->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION &&
		(int)command.sequence >= client->private_resume_first_sequence)
		client->private_input_phase = PRIVATE_INPUT_RUNNING;
	client->private_pmove_last_cmd = command;
	client->private_pmove_last_cmd_valid = true;
	client->private_pmove_jump_secs = terminal_completed ? 0.0f : result_jump_secs;
	client->private_pmove_waterjump_secs = terminal_completed ? 0.0f : result_waterjump_secs;
	if (terminal_completed && !terminal_native_completed)
		SV_ResetGorillaClient (client);
	client->private_pmove_credit_msec -= command.msec;
	if (terminal_native_completed || client->private_pmove_credit_msec < 0.000001)
		client->private_pmove_credit_msec = 0;
	client->cmd = command;
	client->cmd.impulse = 0;
	VectorClear (client->cmd.vr_roomscalemove);
	ent->v.impulse = 0;
	command_completed = true;

cleanup:
	if (suppress_trigger && !ent->free)
		ent->v.button0 = (command.buttons & BUTTON_ATTACK) != 0;
	/* Only accepted commands refresh the pre-death snapshot.  The refresh
	 * helper also requires an active, spawned, living client. */
	if (command_completed && !terminal_native_completed &&
		client->active && client->spawned &&
		client->edict == ent && !ent->free)
		SV_CoopRespawnRefreshClientInventory (ent);
	if (failure)
		SV_PrivateWalkTrialDrop (client, failure);
	/* Impulses are one-shot even when maintenance has no accepted movement. */
	if (client->active)
		ent->v.impulse = 0;
	pmove = saved_pmove;
	movevars = saved_movevars;
	host_frametime = saved_host_frametime;
	pr_global_struct->frametime = saved_qc_frametime;
	host_client = saved_host_client;
	sv_player = saved_sv_player;
	ED_Release (ent);
	return command_completed;
}

typedef struct
{
	edict_t *player;
	edict_t *impacts[8];
	int num_impacts;
} sv_gorilla_trace_context_t;

static vr_gorilla_trace_t SV_GorillaTrace (void *context,
	const float *start, const float *end, int body)
{
	sv_gorilla_trace_context_t *ctx = context;
	edict_t *player = ctx->player;
	vec3_t from, to;
	trace_t trace;
	vr_gorilla_trace_t result;
	int i;

	VectorCopy (start, from);
	VectorCopy (end, to);
	trace = SV_Move (from, body ? player->v.mins : vec3_origin,
		body ? player->v.maxs : vec3_origin, to,
		body ? MOVE_NORMAL : MOVE_NOMONSTERS, player);
	memset (&result, 0, sizeof (result));
	result.fraction = trace.fraction;
	result.startsolid = trace.startsolid;
	result.allsolid = trace.allsolid;
	VectorCopy (trace.endpos, result.end);
	VectorCopy (trace.plane.normal, result.normal);
	result.entity = trace.ent ? NUM_FOR_EDICT (trace.ent) : -1;
	if (body && trace.ent && trace.fraction < 1)
	{
		for (i = 0; i < ctx->num_impacts; i++)
			if (ctx->impacts[i] == trace.ent)
				break;
		if (i == ctx->num_impacts && i < countof (ctx->impacts))
			ctx->impacts[ctx->num_impacts++] = trace.ent;
	}
	return result;
}

void SV_GorillaInvalidateSurface (edict_t *surface)
{
	int model, number, i;
	if (!surface || qcvm != &sv.qcvm || !svs.clients)
		return;
	model = (int)surface->v.modelindex;
	if (model <= 0 || model >= MAX_MODELS || !sv.models[model] ||
		sv.models[model]->type != mod_brush)
		return;
	number = NUM_FOR_EDICT (surface);
	for (i = 0; i < svs.maxclients; i++)
	{
		client_t *client = &svs.clients[i];
		if (client->vr_gorilla_state.surface[0] == number ||
			client->vr_gorilla_state.surface[1] == number)
			SV_GorillaInvalidateAccepted (client);
	}
}

/* Brush anchors use the same origin-only basis as the native body trace. */
static int SV_GorillaSurface (void *context, int entity, unsigned int *model,
	const float *point, float *out, int to_world)
{
	edict_t *surface;
	unsigned int index;
	(void)context;
	if (entity <= 0 || entity >= qcvm->num_edicts)
		return 0;
	surface = EDICT_NUM (entity);
	index = (unsigned int)surface->v.modelindex;
	if (surface->free || surface->v.solid != SOLID_BSP || !index ||
		index >= MAX_MODELS || !sv.models[index] ||
		sv.models[index]->type != mod_brush ||
		(to_world && *model != index) || !VRG_Finite (surface->v.origin))
		return 0;
	*model = index;
	if (to_world)
		VectorAdd (point, surface->v.origin, out);
	else
		VectorSubtract (point, surface->v.origin, out);
	return VRG_Finite (out);
}

static qboolean SV_GorillaCallbackMoved (client_t *client, edict_t *ent)
{
	vec3_t delta;
	if (!client->vr_gorilla_state.initialized)
		return false;
	if (ent->free || ent->v.health <= 0 || ent->v.deadflag)
		return true;
	VectorSubtract (ent->v.origin, client->vr_gorilla_state.origin, delta);
	return !VRG_Finite (delta) || VectorLength (delta) > .01f;
}

/* Touch only native brush buttons. Their QC callback may relocate the body. */
static qboolean SV_GorillaTouchButtons (client_t *client, const int contacts[2])
{
	edict_t *player = client->edict;
	int hand;
	for (hand = 0; hand < 2; hand++)
	{
		int number = contacts[hand];
		edict_t *button = number > 0 && number < qcvm->num_edicts ?
			EDICT_NUM (number) : NULL;
		if (!SV_GorillaButtonTouchAllowed (button))
			number = 0;
		if (number && number != client->vr_gorilla_button[hand])
		{
			int saved_self = pr_global_struct->self;
			int saved_other = pr_global_struct->other;
			float saved_time = pr_global_struct->time;
			client->vr_gorilla_button[hand] = number;
			ED_Retain (button);
			pr_global_struct->self = EDICT_TO_PROG (button);
			pr_global_struct->other = EDICT_TO_PROG (player);
			pr_global_struct->time = qcvm->time;
			PR_ExecuteProgram (button->v.touch);
			pr_global_struct->self = saved_self;
			pr_global_struct->other = saved_other;
			pr_global_struct->time = saved_time;
			ED_Release (button);
			if (!client->active || !client->spawned || client->edict != player ||
				player->free || !client->vr_gorilla_cursor_valid ||
				!client->vr_gorilla_state.initialized ||
				SV_GorillaCallbackMoved (client, player))
			{
				SV_GorillaInvalidateAccepted (client);
				return false;
			}
		}
		client->vr_gorilla_button[hand] = number;
	}
	return true;
}

/* Drain the existing private command FIFO before the one native WALK/FLY
 * physics pass. The hand solver only adds constraints/impulses; it does not
 * take over the server frame, QuakeC lifecycle, or movement clock. */
static qboolean SV_PrepareGorilla (edict_t *ent, client_t *client,
	sv_client_move_frame_t *move_frame, int completed_move,
	qboolean *swim_intent)
{
	unsigned int offset;
	qboolean braced = client->vr_gorilla_state.initialized &&
		client->vr_gorilla_state.touching &&
		VectorLength (ent->v.velocity) < 1 && SV_GorillaEligible (client);
	usercmd_t saved_cmd = client->cmd;
	eval_t *gravity_field = GetEdictFieldValue (ent, qcvm->extfields.gravity);
	float gravity = sv_gravity.value *
		(gravity_field && gravity_field->_float ? gravity_field->_float : 1);

	if (!client->vr_gorilla_capable || client->protocol_qsvr != QSVR_PROTOCOL_PINNED ||
		!sv_gorilla.value || ent->free || ent->v.health <= 0)
	{
		SV_ResetGorillaClient (client);
		*swim_intent = false;
		return false;
	}
	*swim_intent = false;
	SV_CheckWater (ent);
	for (offset = 0; offset < client->private_cmd_queue_count; offset++)
	{
		const usercmd_t *sample = &client->private_cmd_queue[
			(client->private_cmd_queue_head + offset) % SV_PRIVATE_CMD_QUEUE_SIZE];
		sv_gorilla_trace_context_t context = {0};
		vr_gorilla_result_t result;
		vec3_t native_velocity;
		int i;
		if ((int)sample->sequence > completed_move)
			break;
		if (client->vr_gorilla_cursor_valid &&
			(int)sample->sequence <= client->vr_gorilla_last_sequence)
			continue;
		if (client->vr_gorilla_cursor_valid &&
			(int)sample->sequence != client->vr_gorilla_last_sequence + 1)
		{
			VRG_Reset (&client->vr_gorilla_state);
			memset (client->vr_gorilla_button, 0, sizeof (client->vr_gorilla_button));
		}
		client->vr_gorilla_last_sequence = (int)sample->sequence;
		client->vr_gorilla_cursor_valid = true;
		client->cmd = *sample;
		if ((int)sample->sequence <= client->private_discarded_move ||
			!SV_GorillaEligible (client))
		{
			VRG_Reset (&client->vr_gorilla_state);
			memset (client->vr_gorilla_button, 0, sizeof (client->vr_gorilla_button));
			braced = false;
			continue;
		}
		context.player = ent;
		VectorCopy (ent->v.velocity, native_velocity);
		result = VRG_Step (&client->vr_gorilla_state, &sample->vr_gorilla,
			ent->v.origin, ent->v.velocity, sample->seconds, gravity,
			&context, SV_GorillaTrace, SV_GorillaSurface);
		if (((int)ent->v.flags & FL_WATERJUMP) ||
			(ent->v.waterlevel >= 2 && !result.launched))
		{
			VectorCopy (native_velocity, ent->v.velocity);
			result.braced = false;
		}
		if (result.stepped && ent->v.waterlevel >= 2 &&
			!((int)ent->v.flags & FL_WATERJUMP))
		{
			unsigned int liquid = 0, solid = 0;
			for (i = 0; i < 2; i++)
			{
				vec3_t palm;
				int contents;
				VectorAdd (ent->v.origin, sample->vr_gorilla.hand[i], palm);
				contents = SV_PointContents (palm);
				if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME ||
					contents == CONTENTS_LAVA)
					liquid |= 1u << i;
				if (result.contact[i] >= 0)
					solid |= 1u << i;
			}
			*swim_intent |= VRG_SwimImpulse (&sample->vr_gorilla, liquid,
				solid, sample->seconds, sv_maxspeed.value * .7f, ent->v.velocity);
		}
		braced = result.braced;
		if (result.launched && ent->v.velocity[2] > .01f &&
			!((int)ent->v.flags & FL_WATERJUMP))
		{
			ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
			SV_UpdateClientMoveFrameAfterQC (ent, move_frame);
		}
		SV_LinkEdict (ent, false);
		for (i = 0; i < context.num_impacts && !ent->free; i++)
		{
			if (!context.impacts[i]->free)
				SV_Impact (ent, context.impacts[i]);
			if (SV_GorillaCallbackMoved (client, ent))
				break;
		}
		if (!client->active || ent->free || !client->vr_gorilla_cursor_valid ||
			!client->vr_gorilla_state.initialized ||
			SV_GorillaCallbackMoved (client, ent) ||
			!SV_GorillaEligible (client) ||
			!SV_GorillaTouchButtons (client, result.contact))
		{
			SV_GorillaInvalidateAccepted (client);
			braced = false;
			break;
		}
		SV_CheckWater (ent);
	}
	client->cmd = saved_cmd;
	if (*swim_intent && ent->v.waterlevel >= 2)
		braced = false;
	return braced;
}

/* The existing world-frame QuakeC and native movement owner. Pass the exact
 * sequence whose input this frame consumed; the selected terminal adapter
 * must never substitute an accepted but unconsumed tail.
 * Return success only if PostThink and the owner lifetime completed. */
static qboolean SV_Physics_ClientNativeFromPhase (edict_t *ent, int num,
	int completed_move, sv_client_native_start_t start,
	qboolean prior_weapon_think_ran, sv_client_think_window_t *think_window)
{
	sv_client_move_frame_t move_frame;
	sv_vr_weapon_pose_scope_t weapon_scope;
	client_t *client = &svs.clients[num - 1];
	edict_t				  *retained_pusher;
	int movetype, dispatch_movetype;
	qboolean frame_completed = false;
	qboolean suppress_trigger = false, saved_button0 = false;
	qboolean gorilla_braced = false;
	qboolean gorilla_swim_intent = false;
	qboolean gorilla_dispatch, weapon_think_ran = prior_weapon_think_ran;
	qboolean friendly_fire_scope;
	vec3_t callback_origin, callback_delta;

	ED_Retain (ent);
	if (start == SV_CLIENT_NATIVE_FRESH &&
		svs.clients[num - 1].protocol_qsvr == QSVR_PROTOCOL_PINNED)
		SV_ApplyPrivateRoomScaleMove (ent, &svs.clients[num - 1]);
	SV_CaptureClientMoveFrameBeforeQC (ent, &move_frame);
	retained_pusher = move_frame.pusher;
	if (retained_pusher)
		ED_Retain (retained_pusher);

	//
	// call standard client pre-think
	//
	saved_button0 = ent->v.button0 != 0;
	if (start == SV_CLIENT_NATIVE_AFTER_PRETHINK)
	{
		VectorCopy (ent->v.origin, callback_origin);
		goto after_prethink;
	}
	if (start == SV_CLIENT_NATIVE_AFTER_WEAPON_THINK)
	{
		/* The selected owner entered weapon Think from stock WALK. Native
		 * dispatch retains that pre-Think type unless Gorilla reselects. */
		movetype = dispatch_movetype = MOVETYPE_WALK;
		gorilla_dispatch = client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
			client->vr_gorilla_capable && sv_gorilla.value;
		VectorCopy (ent->v.origin, callback_origin);
		goto after_weapon_think;
	}
	if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED)
		suppress_trigger = SV_VRMeleeSuppressNativeTrigger (client, ent,
			&client->cmd);
	if (suppress_trigger)
		ent->v.button0 = 0;
	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	SV_CoopRespawnRefreshClientInventory (ent);
	SV_VRMeleeRefreshTriggerSuppression (client, ent,
		&client->cmd, &suppress_trigger);
	VectorCopy (ent->v.origin, callback_origin);
	PR_ExecuteProgram (pr_global_struct->PlayerPreThink);

after_prethink:
	assert_always (!ent->free);
	VectorSubtract (ent->v.origin, callback_origin, callback_delta);
	if (VectorLength (callback_delta) > .01f)
		SV_GorillaInvalidateAccepted (client);
	SV_GorillaLatchLadder (client, false);
	SV_GorillaResumeDeferredMove (client);

	SV_UpdateClientMoveFrameAfterQC (ent, &move_frame);
	if (ent->v.health <= 0 || ent->v.deadflag ||
		((int)ent->v.movetype != MOVETYPE_WALK &&
		 (int)ent->v.movetype != MOVETYPE_FLY))
		SV_ResetGorillaClient (client);

	//
	// do a move
	//
	SV_CheckVelocity (ent);

	/* Reuse the QSS-M native callback boundary after PreThink and velocity
 * validation, before scheduled Think or any engine/Gorilla movement. */
	if (SV_RunCustomPhysics (ent))
	{
		SV_ResetGorillaClient (client);
		if (ent->free || !client->active || client->edict != ent)
			goto done;
		goto after_native_move;
	}

	/* Match desktop dispatch by retaining the type selected before the
	 * scheduled weapon think. Configured Gorilla clients may still reselect
	 * after their hand-contact callbacks have run. */
	movetype = (int)ent->v.movetype;
	dispatch_movetype = movetype;
	gorilla_dispatch = client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		client->vr_gorilla_capable && sv_gorilla.value;
	SV_VRMeleeRefreshTriggerSuppression (client, ent,
		&client->cmd, &suppress_trigger);
	VectorCopy (ent->v.origin, callback_origin);
	switch (movetype)
	{
	case MOVETYPE_NONE:
	case MOVETYPE_WALK:
	case MOVETYPE_FLY:
	case MOVETYPE_NOCLIP:
		weapon_think_ran = ent->v.nextthink > 0 &&
			ent->v.nextthink <= qcvm->time + host_frametime;
		if (!SV_RunClientWeaponThink (ent, client, &client->cmd, think_window))
			goto done;
		if (think_window)
			weapon_think_ran = true; // opportunity consumed, whether due or not
		break;
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_GIB:
		break;
	default:
		Host_EndGame ("SV_Physics_client: bad movetype %i", (int)ent->v.movetype);
	}
after_weapon_think:
	VectorSubtract (ent->v.origin, callback_origin, callback_delta);
	if (VectorLength (callback_delta) > .01f)
		SV_GorillaInvalidateAccepted (client);
	if ((int)ent->v.movetype == MOVETYPE_WALK ||
		(int)ent->v.movetype == MOVETYPE_FLY)
	{
		gorilla_braced = SV_PrepareGorilla (ent, client, &move_frame,
			completed_move, &gorilla_swim_intent);
		if (ent->free || !client->active)
			goto done;
		SV_GorillaConsumeWater (client, gorilla_swim_intent);
		SV_UpdateClientMoveFrameAfterQC (ent, &move_frame);
	}
	else
		SV_ResetGorillaClient (client);
	if (gorilla_dispatch)
		dispatch_movetype = (int)ent->v.movetype;

	switch (dispatch_movetype)
	{
	case MOVETYPE_NONE:
		break;
	case MOVETYPE_WALK:
		SV_Physics_ClientWalk (ent, &move_frame, gorilla_braced);
		break;
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_GIB:
		SV_Physics_Toss (ent,
			!SV_TakeClientThinkWindow (think_window) || weapon_think_ran);
		break;
	case MOVETYPE_FLY:
		SV_FlyMove (ent, host_frametime, NULL, NULL, true);
		break;
	case MOVETYPE_NOCLIP:
		VectorMA (ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);
		if (!SV_TestEntityPosition (ent))
			VectorCopy (ent->v.origin, ent->v.oldorigin);
		break;
	default:
		Host_EndGame ("SV_Physics_client: bad movetype %i", (int)ent->v.movetype);
	}

after_native_move:
	//
	// call standard player post-think
	//
	/* Native gravity and collision still move the body after hand solving.
	 * Commit that baseline before callbacks, then reject callback relocations. */
	if (client->vr_gorilla_state.initialized)
		VectorCopy (ent->v.origin, client->vr_gorilla_state.origin);
	SV_LinkEdict (ent, true);

	assert_always (!ent->free);

	pr_global_struct->time = qcvm->time;
	pr_global_struct->self = EDICT_TO_PROG (ent);
	SV_BeginPrivateVRWeaponPose (ent, client, &client->cmd, &weapon_scope);
	SV_VRMeleeRefreshTriggerSuppression (client, ent,
		&client->cmd, &suppress_trigger);
	friendly_fire_scope = SV_CoopFriendlyFireBegin (ent);
	PR_ExecuteProgram (pr_global_struct->PlayerPostThink);
	if (friendly_fire_scope)
		SV_CoopFriendlyFireEnd ();
	SV_EndPrivateVRWeaponPose (ent, &weapon_scope);
	if (client->protocol_qsvr == QSVR_PROTOCOL_PINNED &&
		!SV_VRContactDrainQueued (ent, client, completed_move))
		goto done;
	if (client->vr_gorilla_state.initialized &&
		(!SV_GorillaEligible (client) || SV_GorillaCallbackMoved (client, ent)))
		SV_ResetGorillaClient (client);
	SV_CoopSharedObserveClientDeath (ent, num);
	frame_completed = true;

done:
	if (suppress_trigger && !ent->free)
		ent->v.button0 = saved_button0;
	const qboolean owner_completed = frame_completed && client->active &&
		client->spawned && client->edict == ent && !ent->free;
	/* PlayerPostThink and the weapon think above may both update inventory. */
	if (owner_completed)
		SV_CoopRespawnRefreshClientInventory (ent);
	if (owner_completed && client->protocol_qsvr == QSVR_PROTOCOL_PINNED)
		client->private_completed_move = completed_move;
	if (retained_pusher)
		ED_Release (retained_pusher);
	ED_Release (ent);
	return owner_completed;
}

static qboolean SV_Physics_ClientNativeFrame (edict_t *ent, int num,
	int completed_move)
{
	return SV_Physics_ClientNativeFromPhase (ent, num, completed_move,
		SV_CLIENT_NATIVE_FRESH, false, NULL);
}

/* A native selected owner uses the ordinary world-frame dispatcher from its
 * beginning. Coalesce only the command levels/latches that the ordinary
 * private parser would have staged; physical contacts retain their ordered
 * queue cursor and are invalidated while the owner is still dead. */
static void SV_Physics_ClientSelectedNativeFrame (edict_t *ent, int num,
	client_t *client, qboolean terminal)
{
	client_t *saved_host_client = host_client;
	edict_t *saved_sv_player = sv_player;
	usercmd_t staged, last = {0};
	int completed_move = client->private_completed_move;
	unsigned int offset, latched_buttons = 0;
	int latched_impulse = 0;
	qboolean consumed = false;
	vec3_t roomscale = {0, 0, 0};
	const char *failure;

	if (!terminal && (failure = SV_PrivateWalkTrialFrameStateError (ent, client,
		&client->cmd)) != NULL)
	{
		SV_PrivateWalkTrialDrop (client, failure);
		return;
	}

	if (client->private_cmd_queue_count > SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_head >= SV_PRIVATE_CMD_QUEUE_SIZE ||
		client->private_cmd_queue_msec > SV_PRIVATE_CMD_QUEUE_MAX_MSEC)
	{
		SV_PrivateWalkTrialDrop (client, "invalid native command queue");
		return;
	}
	if (client->private_pmove_last_cmd_valid)
		last = client->private_pmove_last_cmd;
	else
		VectorCopy (ent->v.v_angle, last.viewangles);
	for (offset = 0; offset < client->private_cmd_queue_count; ++offset)
	{
		const usercmd_t *queued = &client->private_cmd_queue[
			(client->private_cmd_queue_head + offset) % SV_PRIVATE_CMD_QUEUE_SIZE];
		if (queued->msec < 1 || queued->msec > 125 ||
			(int)queued->sequence <= completed_move ||
			(int)queued->sequence > client->lastmovemessage)
		{
			SV_PrivateWalkTrialDrop (client, "invalid native command order");
			return;
		}
		completed_move = (int)queued->sequence;
		latched_buttons |= queued->buttons & 3;
		if (queued->impulse)
			latched_impulse = queued->impulse;
		if (!queued->vr_active)
			VectorClear (roomscale);
		else
			VectorAdd (roomscale, queued->vr_roomscalemove, roomscale);
		last = *queued;
		consumed = true;
	}
	staged = last;
	staged.buttons |= latched_buttons;
	staged.impulse = latched_impulse;
	staged.seconds = 0; // the native dispatcher owns the world-frame clock
	staged.msec = 0;
	if (terminal)
		VectorClear (staged.vr_roomscalemove);
	else
		VectorCopy (roomscale, staged.vr_roomscalemove);
	client->cmd = staged;
	VectorCopy (staged.viewangles, ent->v.v_angle);
	ent->v.button0 = (staged.buttons & BUTTON_ATTACK) != 0;
	ent->v.button2 = (staged.buttons & 2) != 0;
	SV_SetClientExtraButtons (ent, staged.buttons);
	ent->v.impulse = staged.impulse;
	host_client = client;
	sv_player = ent;
	client->private_move_native_frame = true;
	client->private_pmove_credit_msec = 0.0;
	client->private_pmove_jump_secs = 0.0f;
	client->private_pmove_waterjump_secs = 0.0f;
	if (terminal)
	{
		SV_ClientUpdateAnglesForClient (client);
		/* Drain while dead, before queued respawn input can revive the owner. */
		if (!SV_VRContactDrainQueued (ent, client, completed_move))
		{
			SV_PrivateWalkTrialDrop (client, "terminal contact cursor invalidated");
			host_client = saved_host_client;
			sv_player = saved_sv_player;
			return;
		}
		SV_ResetGorillaClient (client);
		client->vr_gorilla_last_sequence = completed_move;
		client->vr_gorilla_cursor_valid = true;
	}
	else
		SV_ClientThink (); // includes the single native angle/recoil update
	if (SV_Physics_ClientNativeFrame (ent, num, completed_move) && consumed)
	{
		if (client->private_input_phase == PRIVATE_INPUT_AWAIT_COMPLETION &&
			completed_move >= client->private_resume_first_sequence)
			client->private_input_phase = PRIVATE_INPUT_RUNNING;
		last.impulse = 0;
		VectorClear (last.vr_roomscalemove);
		client->private_pmove_last_cmd = last;
		client->private_pmove_last_cmd_valid = true;
	}
	/* Selected frame-end cleanup restores levels but does not clear the edict's
	 * impulse as the ordinary private path does. Keep it one-shot here. */
	if (!ent->free)
		ent->v.impulse = 0;
	client->cmd.impulse = 0;
	host_client = saved_host_client;
	sv_player = saved_sv_player;
}

static void SV_Physics_Client (edict_t *ent, int num)
{
	client_t *client = &svs.clients[num - 1];
	unsigned queue_offset;

	if (!client->active)
		return; // unconnected slot
	client->private_pmove_pusher_interaction = false;
	if (ent->free || ent->v.health <= 0 || ent->v.deadflag != DEAD_NO)
		SV_ClearRecentInstantTeleportTriggerForClientSlot (num - 1);
	SV_VRContactObserveSpawn (client);

	if (!client->knowntoqc && sv_gameplayfix_spawnbeforethinks.value)
		return; // don't spam prethinks before we called putclientinserver.
	if (SV_PrivateWalkTrialSelected (client) &&
		(SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_TERMINAL ||
		 SV_PrivateWalkTrialClassifyState (client) == SV_PRIVATE_MOVE_NATIVE))
	{
		SV_Physics_ClientSelectedNativeFrame (ent, num, client,
			SV_PrivateWalkTrialTerminalState (client));
		return;
	}

	if (SV_PrivateWalkTrialSelected (client))
	{
		sv_client_think_window_t think_window = {
			true, host_frametime, pr_global_struct->frametime};
		/* Bound catch-up work while preserving each command's QC lifecycle. */
		for (queue_offset = 0; queue_offset < 8; queue_offset++)
		{
			if (!client->active || !SV_PrivateWalkTrialSelected (client) ||
				!SV_Physics_ClientPrivateWalkTrial (ent, client,
					queue_offset, queue_offset == 0, &think_window))
				break;
			if (SV_PrivateWalkTrialTerminalState (client) ||
				client->private_move_native_frame)
				break;
		}
		return;
	}

	SV_Physics_ClientNativeFrame (ent, num, client->lastmovemessage);
}

//============================================================================

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
static void SV_Physics_None (edict_t *ent)
{
	// regular thinking
	SV_RunThink (ent);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
static void SV_Physics_Noclip (edict_t *ent)
{
	// regular thinking
	if (!SV_RunThink (ent))
		return;

	// stationary: the move below would be an exact no-op, skip the relink (and its BSP leaf walk)
	if (!ent->v.velocity[0] && !ent->v.velocity[1] && !ent->v.velocity[2] && !ent->v.avelocity[0] && !ent->v.avelocity[1] && !ent->v.avelocity[2])
		return;

	VectorMA (ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);
	VectorMA (ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);

	SV_LinkEdict (ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
void SV_CheckWaterTransition (edict_t *ent)
{
	int cont;

	cont = SV_PointContents (ent->v.origin);

	if (!ent->v.watertype)
	{ // just spawned here
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
		return;
	}

	if (cont <= CONTENTS_WATER)
	{
		if (ent->v.watertype == CONTENTS_EMPTY)
		{ // just crossed into water
			SV_StartSound (ent, NULL, 0, "misc/h2ohit1.wav", 255, 1);
		}
		ent->v.watertype = cont;
		ent->v.waterlevel = 1;
	}
	else
	{
		if (ent->v.watertype != CONTENTS_EMPTY)
		{ // just crossed into water
			SV_StartSound (ent, NULL, 0, "misc/h2ohit1.wav", 255, 1);
		}
		ent->v.watertype = CONTENTS_EMPTY;
		ent->v.waterlevel = cont;
	}
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
static void SV_Physics_Toss (edict_t *ent, qboolean think_already_ran)
{
	trace_t trace;
	vec3_t	end, move_velocity;
	float	backoff;

	// regular thinking
	if (!think_already_ran && !SV_RunThink (ent))
		return;

	// if onground, return without moving
	if (((int)ent->v.flags & FL_ONGROUND))
		return;

	SV_CheckVelocity (ent);

	// add gravity
	if (ent->v.movetype != MOVETYPE_FLY && ent->v.movetype != MOVETYPE_FLYMISSILE)
		SV_AddGravity (ent, move_velocity);
	else
		VectorCopy (ent->v.velocity, move_velocity);

	// move angles
	VectorMA (ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);

	// move origin
	VectorMA (ent->v.origin, host_frametime, move_velocity, end);
	trace = SV_PushEntityTo (ent, end, true);

	if (ent->free)
		return;

	if (trace.fraction == 1)
		return;

	if (ent->v.movetype == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else
		backoff = 1;

	ClipVelocity (ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

	// stop if on ground
	if (trace.plane.normal[2] > MIN_WALK_NORMAL)
	{
		if (ent->v.movetype != MOVETYPE_BOUNCE ||
			(sv_gameplayfix_bouncedownslopes.value ? DotProduct (trace.plane.normal, ent->v.velocity) : ent->v.velocity[2]) < 60)
		{
			ent->v.flags = (int)ent->v.flags | FL_ONGROUND;

			// SV_PushEntityTo() calls SV_LinkEdict (true) that could free trace.ent
			if (trace.ent && !trace.ent->free)
				ent->v.groundentity = EDICT_TO_PROG (trace.ent);

			VectorCopy (vec3_origin, ent->v.velocity);
			VectorCopy (vec3_origin, ent->v.avelocity);
		}
	}

	// check for in water
	SV_CheckWaterTransition (ent);
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
=============
*/
static void SV_Physics_Step (edict_t *ent)
{
	qboolean hitsound;
	vec3_t	 move_velocity;

	// freefall if not onground
	if (!((int)ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM)))
	{
		if (ent->v.velocity[2] < sv_gravity.value * -0.1)
			hitsound = true;
		else
			hitsound = false;

		SV_AddGravity (ent, move_velocity);
		SV_CheckVelocity (ent);
		// Bound the sweep as well as the stored end velocity after gravity.
		for (int i = 0; i < 3; i++)
		{
			if (IS_NAN (move_velocity[i]))
				move_velocity[i] = 0;
			if (move_velocity[i] > sv_maxvelocity.value)
				move_velocity[i] = sv_maxvelocity.value;
			else if (move_velocity[i] < -sv_maxvelocity.value)
				move_velocity[i] = -sv_maxvelocity.value;
		}
		SV_FlyMove (ent, host_frametime, move_velocity, NULL, true);
		SV_LinkEdict (ent, true);

		if (ent->free)
			return;

		if ((int)ent->v.flags & FL_ONGROUND) // just hit ground
		{
			if (hitsound)
				SV_StartSound (ent, NULL, 0, "demon/dland2.wav", 255, 1);
		}
	}

	// regular thinking
	if (SV_RunThink (ent))
		SV_CheckWaterTransition (ent);
}

//============================================================================

// track ED_Alloc during SV_Physics execution
static void SV_Physics_Alloc_Hook (edict_t *e)
{
	// Keep every cached slot attached to the same edict for the rest of this physics frame.
	pushable_ent_cache[num_pushable_ent_cache++] = e;
	ED_Retain (e);
}

/*
================
SV_Physics

================
*/
void SV_Physics (void)
{
	int		 i;
	int		 entity_cap; // For sv_freezenonclients
	edict_t *ent;

	ED_AllocHook_func previous_alloc_hook = NULL;

	int physics_mode;
	SV_CoopFriendlyFireReset ();
	SV_CoopSharedBeginFrameDeathTracking ();
	if (qcvm->extglobals.physics_mode)
		physics_mode = *qcvm->extglobals.physics_mode;
	else
		physics_mode = (qcvm == &cl.qcvm) ? 0 : 2; // csqc doesn't run thinks by default. it was meant to simplify implementations, but we just force fields to
												   // match ssqc so its not that large a burden.

	if (physics_mode)
		SV_BeginPusherSupportFrame ();

	if (!physics_mode)
	{
		SV_CoopSharedEndFrameDeathTracking ();
		qcvm->time += host_frametime;
		SV_CoopFriendlyFireReset ();
		return;
	}
	else if (physics_mode == 1)
	{ // for dp compat. note that this violates MOVETYPE_PUSH.
		for (i = 0, ent = qcvm->edicts; i < qcvm->num_edicts; i++, ent = NEXT_EDICT (ent))
		{
			if (ent->free)
				continue;
			SV_RunThink (ent);
		}
		SV_CoopSharedEndFrameDeathTracking ();
		qcvm->time += host_frametime;
		SV_CoopFriendlyFireReset ();
		return;
	}

	// let the progs know that a new frame has started
	if (pr_global_struct->StartFrame)
	{
		pr_global_struct->self = EDICT_TO_PROG (qcvm->edicts);
		pr_global_struct->other = EDICT_TO_PROG (qcvm->edicts);
		pr_global_struct->time = qcvm->time;
		PR_ExecuteProgram (pr_global_struct->StartFrame);
	}

	// SV_CheckAllEnts ();

	//
	// treat each object in turn
	//
	ent = qcvm->edicts;

	if (sv_freezenonclients.value && qcvm == &sv.qcvm)
		entity_cap = svs.maxclients + 1; // Only run physics on clients and the world
	else
		entity_cap = qcvm->num_edicts;

	// QC can flip the cvars mid-tick, the whole tick must use one consistent decision
	const qboolean fast_pushers = (sv_fastpushmove.value > 0.f);
	sv_analyticphysics_frame = (sv_analyticphysics.value > 0.f);

	// fill the pushable entities cache and the spatial grid over it
	if (fast_pushers)
	{
		double build_start = 0;
		if (sv_speeds.value && qcvm == &sv.qcvm)
			build_start = Sys_DoubleTime ();

		num_pushable_ent_cache = 0;
		PushGrid_Clear ();
		// beware, we skip entity 0 here:
		edict_t *check = NEXT_EDICT (qcvm->edicts);
		for (int e = 1; e < qcvm->num_edicts; e++, check = NEXT_EDICT (check))
		{
			if (check->free)
				continue;
			if (!SV_IsPushable (check))
				continue;

			pushable_ent_cache[num_pushable_ent_cache++] = check;
			ED_Retain (check);
			PushGrid_Insert (check);
		}
		push_grid_tail_start = num_pushable_ent_cache;
		push_grid_qcvm = qcvm;
		push_grid_active = true;

		if (sv_speeds.value && qcvm == &sv.qcvm)
		{
			sv_speeds_build_ms += (Sys_DoubleTime () - build_start) * 1000.0;
			sv_speeds_pushables += num_pushable_ent_cache;
			sv_speeds_grid_entries += push_grid_num_entries;
		}

		previous_alloc_hook = ED_AllocSetHook (SV_Physics_Alloc_Hook);
	}

	// for (i=0 ; i<sv.num_edicts ; i++, ent = NEXT_EDICT(ent))
	for (i = 0; i < entity_cap; i++, ent = NEXT_EDICT (ent))
	{
		if (ent->free)
			continue;

		if (pr_global_struct->force_retouch)
		{
			SV_LinkEdict (ent, true); // force retouch even for stationary

			if (ent->free)
				continue;
		}

		// Release only support established by the private pusher record. QuakeC
		// is otherwise free to give FL_ONGROUND/groundentity custom semantics.
		SV_UpdatePersistentPusherSupport (ent);
		if (SV_MovetypeUsesGroundFlag (ent))
			SV_AdoptPusherSupport (ent);

		if (i > 0 && i <= svs.maxclients && qcvm == &sv.qcvm)
			SV_Physics_Client (ent, i);
		else if (SV_RunCustomPhysics (ent))
		{
			/* The callback owns linking and Think for non-client entities. */
		}
		else if (ent->v.movetype == MOVETYPE_PUSH)
			SV_Physics_Pusher (ent);
		else if (ent->v.movetype == MOVETYPE_NONE)
			SV_Physics_None (ent);
		else if (ent->v.movetype == MOVETYPE_NOCLIP)
			SV_Physics_Noclip (ent);
		else if (ent->v.movetype == MOVETYPE_STEP)
			SV_Physics_Step (ent);
		else if (
			ent->v.movetype == MOVETYPE_TOSS || ent->v.movetype == MOVETYPE_GIB || ent->v.movetype == MOVETYPE_BOUNCE || ent->v.movetype == MOVETYPE_FLY ||
			ent->v.movetype == MOVETYPE_FLYMISSILE)
			SV_Physics_Toss (ent, false);
		else
			Host_EndGame ("SV_Physics: bad movetype %i", (int)ent->v.movetype);

		if (i > 0 && i <= svs.maxclients && qcvm == &sv.qcvm)
			SV_CoopSharedObserveClientDeath (ent, i);

		// johnfitz -- PROTOCOL_FITZQUAKE
		// capture interval to nextthink here and send it to client for better
		// lerp timing; ~0.1 intervals match what the client assumes but thinks
		// fire quantized to server ticks, so the exact value still improves
		// lerp timing where the extra bytes are affordable
		ent->sendinterval = false;
		ent->sendinterval_default = false;
		if (!ent->free && ent->v.nextthink > qcvm->time &&
			(ent->v.movetype == MOVETYPE_STEP || ent->v.movetype == MOVETYPE_WALK || ent->v.frame != ent->oldframe))
		{
			int j = Q_rint ((ent->v.nextthink - ent->oldthinktime) * 255);
			if (j == 25 || j == 26)
				ent->sendinterval_default = true;
			else if (j >= 0 && j < 256)
				ent->sendinterval = true;
		}
		// johnfitz
	}

	/* Later projectiles and entity thinks can kill a client after its pass. */
	SV_CoopSharedEndFrameDeathTracking ();

	if (pr_global_struct->force_retouch)
		pr_global_struct->force_retouch--;

	if (!(sv_freezenonclients.value && qcvm == &sv.qcvm))
		qcvm->time += host_frametime;

	if (fast_pushers)
	{
		push_grid_active = false;
		ED_AllocSetHook (previous_alloc_hook);
		for (i = num_pushable_ent_cache - 1; i >= 0; i--)
			ED_Release (pushable_ent_cache[i]);
	}
	SV_CoopFriendlyFireReset ();
}
