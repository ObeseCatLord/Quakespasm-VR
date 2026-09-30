/*
 * Desktop screen-space weapon wheel. Catalog policy is shared with the
 * inherited QuakeSpasm OpenVR implementation; this adapter reads vkQuake's
 * existing client stats and records only transient UI hover identity.
 */
#include "quakedef.h"
#include "vr_input.h"
#include "vr_weapon_menu.h"
#include "vr_weapon_schema.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

extern qpic_t *Sbar_WeaponMenuIcon (int item_bit);

cvar_t vr_weaponmenu_player_teleport = {"vr_weaponmenu_player_teleport", "1", CVAR_ARCHIVE};

#define VR_WEAPON_MENU_MAX_ENTRIES VR_WEAPON_CATALOG_MAX_OBSERVATIONS
#define VR_WEAPON_MENU_PLAYSPACE_MESH_SCALE 0.28f

typedef struct {
	const vr_weapon_menu_entry_t *entry;
	qboolean active;
	qboolean selectable;
	int ammo;
	int ammo_max;
	float center_x;
	float center_y;
	float left;
	float top;
	float width;
	float height;
} vr_weapon_menu_visible_t;

static const vr_weapon_menu_entry_t vr_weapon_menu_stock_entries[] = {
	{IT_AXE, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"AXE", "progs/g_axe.mdl", 1.0f, {0, 0, 0}, IT_AXE, 1,
		STAT_ITEMS, IT_AXE, STAT_ACTIVEWEAPON, IT_AXE, -1, 0, -1,
		"progs/v_axe.mdl"},
	{IT_SHOTGUN, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"SHOTGUN", "progs/v_shot.mdl", 1.0f, {0, 0, 0}, IT_SHOTGUN, 2,
		STAT_ITEMS, IT_SHOTGUN, STAT_ACTIVEWEAPON, IT_SHOTGUN,
		STAT_SHELLS, 100, STAT_VR_MAX_SHELLS, "progs/v_shot.mdl"},
	{IT_SUPER_SHOTGUN, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"DOUBLE SHOTGUN", "progs/g_shot.mdl", 1.0f, {0, 0, 0}, IT_SUPER_SHOTGUN, 3,
		STAT_ITEMS, IT_SUPER_SHOTGUN, STAT_ACTIVEWEAPON, IT_SUPER_SHOTGUN,
		STAT_SHELLS, 100, STAT_VR_MAX_SHELLS, "progs/v_shot2.mdl"},
	{IT_NAILGUN, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"NAILGUN", "progs/g_nail.mdl", 1.0f, {0, 0, 0}, IT_NAILGUN, 4,
		STAT_ITEMS, IT_NAILGUN, STAT_ACTIVEWEAPON, IT_NAILGUN,
		STAT_NAILS, 200, STAT_VR_MAX_NAILS, "progs/v_nail.mdl"},
	{IT_SUPER_NAILGUN, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"SUPER NAILGUN", "progs/g_nail2.mdl", 1.0f, {0, 0, 0}, IT_SUPER_NAILGUN, 5,
		STAT_ITEMS, IT_SUPER_NAILGUN, STAT_ACTIVEWEAPON, IT_SUPER_NAILGUN,
		STAT_NAILS, 200, STAT_VR_MAX_NAILS, "progs/v_nail2.mdl"},
	{IT_GRENADE_LAUNCHER, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"GRENADE", "progs/g_rock.mdl", 1.0f, {0, 0, 0}, IT_GRENADE_LAUNCHER, 6,
		STAT_ITEMS, IT_GRENADE_LAUNCHER, STAT_ACTIVEWEAPON, IT_GRENADE_LAUNCHER,
		STAT_ROCKETS, 100, STAT_VR_MAX_ROCKETS, "progs/v_rock.mdl"},
	{IT_ROCKET_LAUNCHER, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"ROCKET", "progs/g_rock2.mdl", 1.0f, {0, 0, 0}, IT_ROCKET_LAUNCHER, 7,
		STAT_ITEMS, IT_ROCKET_LAUNCHER, STAT_ACTIVEWEAPON, IT_ROCKET_LAUNCHER,
		STAT_ROCKETS, 100, STAT_VR_MAX_ROCKETS, "progs/v_rock2.mdl"},
	{IT_LIGHTNING, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"LIGHTNING", "progs/g_light.mdl", 1.0f, {0, 0, 0}, IT_LIGHTNING, 8,
		STAT_ITEMS, IT_LIGHTNING, STAT_ACTIVEWEAPON, IT_LIGHTNING,
		STAT_CELLS, 100, STAT_VR_MAX_CELLS, "progs/v_light.mdl"}
};

static const vr_weapon_menu_catalog_t vr_weapon_menu_stock_catalog = {
	vr_weapon_menu_stock_entries,
	sizeof (vr_weapon_menu_stock_entries) / sizeof (vr_weapon_menu_stock_entries[0]),
	0
};

typedef struct {
	int selector;
	int replace_selector;
	int impulse;
	const char *label;
	const char *model_path;
	int owned_stat;
	int owned_mask;
	int active_stat;
	int active_mask;
	int ammo_stat;
	int ammo_max;
	const char *viewmodel_path;
} vr_weapon_menu_profile_entry_t;

#define VR_PROFILE(selector, impulse, label, model, ammo, max) \
	{selector, 0, impulse, label, model, -1, 0, -1, 0, ammo, max, NULL}

#define VR_NATIVE_PROFILE(stat, selector, impulse, label, model, held, ammo, max) \
	{selector, 0, impulse, label, model, stat, selector, STAT_ACTIVEWEAPON, selector, ammo, max, held}

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_ad_profile[] = {
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_AXE, 1, "AXE", "progs/g_axe.mdl", "progs/v_shadaxe0.mdl", -1, 0),
	VR_NATIVE_PROFILE(STAT_ITEMS, 1, 2, "SHOTGUN", "progs/g_shot1.mdl", "progs/v_shot.mdl", STAT_SHELLS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, 2, 3, "DOUBLE SHOTGUN", "progs/g_shot2.mdl", "progs/v_shot2.mdl", STAT_SHELLS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, 4, 4, "NAILGUN", "progs/g_nail.mdl", "progs/v_nail.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_ITEMS, 8, 5, "SUPER NAILGUN", "progs/g_nail2.mdl", "progs/v_nail2.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_ITEMS, 16, 6, "GRENADE", "progs/g_rock.mdl", "progs/v_rock.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, 32, 7, "ROCKET", "progs/g_rock2.mdl", "progs/v_rock2.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, 64, 8, "LIGHTNING", "progs/g_light.mdl", "progs/v_light.mdl", STAT_CELLS, 100)
};

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_dwell_profile[] = {
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_AXE, 1, "AXE", "progs/g_axe.mdl", "progs/v_axe2.mdl", -1, 0),
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_SHOTGUN, 2, "SHOTGUN", "progs/g_shotgn.mdl", "progs/v_shot.mdl", STAT_SHELLS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_SUPER_SHOTGUN, 23, "DOUBLE SHOTGUN", "progs/g_shot.mdl", "progs/v_shot2.mdl", STAT_SHELLS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_NAILGUN, 4, "NAILGUN", "progs/g_nail.mdl", "progs/v_nail.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_SUPER_NAILGUN, 5, "SUPER NAILGUN", "progs/g_nail2.mdl", "progs/v_nail2.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_GRENADE_LAUNCHER, 6, "GRENADE", "progs/g_rock.mdl", "progs/v_rock.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_ROCKET_LAUNCHER, 7, "ROCKET", "progs/g_rock2.mdl", "progs/v_rock2.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, IT_LIGHTNING, 28, "LIGHTNING", "progs/g_light.mdl", "progs/v_light.mdl", STAT_CELLS, 100),
	{128, 0, 33, "ROTARY SHOTGUN", "progs/g_shot3.mdl", STAT_VR_MODITEMS, 4, STAT_ACTIVEWEAPON, 128, STAT_SHELLS, 100, "progs/v_shot3.mdl"},
	{256, 0, 38, "CRYSTAL LANCE", "progs/g_rail.mdl", STAT_VR_MODITEMS, 8, STAT_ACTIVEWEAPON, 256, STAT_CELLS, 100, "progs/v_rail.mdl"},
	{512, 0, 2, "RIFLE", "progs/g_rifle.mdl", STAT_VR_MODITEMS, 32, STAT_ACTIVEWEAPON, 512, STAT_SHELLS, 100, "progs/v_rifle.mdl"}
};

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_alkaline_profile[] = {
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 4096, 1, "AXE", "progs/g_axe_alk.mdl", "progs/v_alkaxe20fps.mdl", -1, 0),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 1, 2, "SHOTGUN", "progs/g_shotgn.mdl", "progs/v_shot40fps.mdl", STAT_SHELLS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 2, 3, "DOUBLE SHOTGUN", "progs/g_shot.mdl", "progs/v_shot2_40fps.mdl", STAT_SHELLS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 4, 4, "NAILGUN", "progs/g_nail.mdl", "progs/v_nail_alk40fps.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 8, 5, "SUPER NAILGUN", "progs/g_nail2.mdl", "progs/v_nail3.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 16, 6, "GRENADE", "progs/g_rock.mdl", "progs/v_rock_40fps.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 32, 7, "ROCKET", "progs/g_rock2.mdl", "progs/v_rock2_40fps.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 64, 228, "LIGHTNING", "progs/g_light.mdl", "progs/v_light.mdl", STAT_CELLS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 256, 226, "SAW", "progs/g_saw.mdl", "progs/v_saw.mdl", -1, 0),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 512, 227, "PLASMA", "progs/g_plasma.mdl", "progs/v_plasma.mdl", STAT_CELLS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 1024, 225, "LASER", "progs/g_laserg.mdl", "progs/v_laserg40fps.mdl", STAT_CELLS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 8192, 229, "MINE", "progs/g_mine.mdl", "progs/v_mine_40fps.mdl", STAT_ROCKETS, 100)
};

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_enyo_profile[] = {
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 4096, 1, "SWORD", "progs/ee_g_sword.mdl", "progs/ee_v_sword.mdl", -1, 0),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 1, 2, "PISTOL", "progs/ee_g_pistol.mdl", "progs/ee_v_pistol.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 2, 3, "SHOTGUN", "progs/ee_g_sgun.mdl", "progs/ee_v_sgun.mdl", STAT_SHELLS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 4, 4, "SMG", "progs/ee_g_smgs.mdl", "progs/ee_v_smgs.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 8, 5, "PLASMA", "progs/ee_g_plasma.mdl", "progs/ee_v_plasma.mdl", STAT_CELLS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 16, 6, "GRENADE", "progs/ee_g_glaunch.mdl", "progs/ee_v_glaunch.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 32, 7, "ROCKET", "progs/ee_g_rlaunch.mdl", "progs/ee_v_rlaunch.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_VR_WEAPONS, 64, 8, "RAILGUN", "progs/ee_g_railgun.mdl", "progs/ee_v_railgun.mdl", STAT_CELLS, 100)
};

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_qbj3_profile[] = {
	VR_PROFILE(4096, 1, "WRENCH", "progs/v_wrench.mdl", -1, 0),
	VR_PROFILE(1, 2, "PISTOL", "progs/v_pistol.mdl", STAT_AMMO, 0),
	VR_PROFILE(2, 3, "FLAK SHOTGUN", "progs/v_flakshotgun.mdl", STAT_SHELLS, 100),
	VR_PROFILE(4, 4, "T NAILGUN", "progs/v_tnailgun.mdl", STAT_NAILS, 300),
	VR_PROFILE(8, 5, "REBAR", "progs/v_rebar.mdl", STAT_NAILS, 300),
	VR_PROFILE(16, 6, "GRENADE", "progs/v_grenlauncher.mdl", STAT_ROCKETS, 100),
	VR_PROFILE(32, 7, "MMML", "progs/v_mmml.mdl", STAT_ROCKETS, 100),
	VR_PROFILE(64, 8, "INVOKER", "progs/v_invoker.mdl", STAT_CELLS, 10)
};

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_mjolnir_profile[] = {
	{128, 0, 81, "LIGHTNING", "progs/drake/g_light2.mdl", 42, 128, STAT_ACTIVEWEAPON, 128, STAT_CELLS, 100},
	{131072, 0, 76, "PROXIMITY", "progs/hipnotic/g_prox.mdl", 42, 131072, STAT_ACTIVEWEAPON, 131072, STAT_ROCKETS, 100},
	{262144, 0, 77, "HAMMER", "progs/violentrumble/g_hammer.mdl", 42, 262144, STAT_ACTIVEWEAPON, 262144, STAT_CELLS, 100},
	{524288, 0, 75, "LASER", "progs/hipnotic/g_laserg.mdl", 42, 524288, STAT_ACTIVEWEAPON, 524288, STAT_CELLS, 100},
	{1048576, 0, 80, "GRAPPLE", "progs/drake/g_grpple.mdl", 42, 1048576, STAT_ACTIVEWEAPON, 1048576, -1, 0},
	{4194304, 0, 79, "WAND", "progs/drake/g_wand.mdl", 42, 4194304, STAT_ACTIVEWEAPON, 4194304, -1, 0}
};

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_mg3_profile[] = {
	{128, 0, 1, "HAMMER", "progs/g_hammer.mdl", STAT_ITEMS, 128, STAT_ACTIVEWEAPON, 128, -1, 0},
	VR_PROFILE(8388608, 225, "LASER", "progs/g_laserg.mdl", STAT_CELLS, 100)
};

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_hipnotic_profile[] = {
	VR_NATIVE_PROFILE(STAT_ITEMS, HIT_MJOLNIR, 1, "MJOLNIR", "progs/g_hammer.mdl", "progs/v_hammer.mdl", -1, 0),
	VR_NATIVE_PROFILE(STAT_ITEMS, HIT_LASER_CANNON, 8, "LASER CANNON", "progs/g_laserg.mdl", "progs/v_laserg.mdl", STAT_CELLS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, HIT_PROXIMITY_GUN, 6, "PROXIMITY", "progs/g_prox.mdl", "progs/v_prox.mdl", STAT_ROCKETS, 100)
};

static const vr_weapon_menu_profile_entry_t vr_weapon_menu_rogue_profile[] = {
	{RIT_AXE, IT_AXE, 1, "AXE", "progs/g_axe.mdl", STAT_ITEMS, RIT_AXE, STAT_ACTIVEWEAPON, RIT_AXE, -1, 0, "progs/v_axe.mdl"},
	VR_NATIVE_PROFILE(STAT_ITEMS, RIT_LAVA_NAILGUN, 60, "LAVA NAILGUN", "progs/v_lava.mdl", "progs/v_lava.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_ITEMS, RIT_LAVA_SUPER_NAILGUN, 61, "LAVA SUPER NAILGUN", "progs/v_lava2.mdl", "progs/v_lava2.mdl", STAT_NAILS, 200),
	VR_NATIVE_PROFILE(STAT_ITEMS, RIT_MULTI_GRENADE, 62, "MULTI GRENADE", "progs/v_multi.mdl", "progs/v_multi.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, RIT_MULTI_ROCKET, 63, "MULTI ROCKET", "progs/v_multi2.mdl", "progs/v_multi2.mdl", STAT_ROCKETS, 100),
	VR_NATIVE_PROFILE(STAT_ITEMS, RIT_PLASMA_GUN, 64, "PLASMA", "progs/v_plasma.mdl", "progs/v_plasma.mdl", STAT_CELLS, 100)
};

#undef VR_NATIVE_PROFILE
#undef VR_PROFILE

static vr_weapon_menu_entry_t vr_weapon_menu_wwheel_entries[VR_WEAPON_MENU_MAX_ENTRIES];
static char vr_weapon_menu_wwheel_labels[VR_WEAPON_MENU_MAX_ENTRIES][32];
static char vr_weapon_menu_schema_models[VR_WEAPON_MENU_MAX_ENTRIES][MAX_QPATH];
static char vr_weapon_menu_runtime_models[VR_WEAPON_MENU_MAX_ENTRIES][MAX_QPATH];
static char vr_weapon_menu_held_models[VR_WEAPON_MENU_MAX_ENTRIES][MAX_QPATH];
static int vr_weapon_menu_schema_bitmasks[VR_WEAPON_MENU_MAX_ENTRIES];
static vr_weapon_menu_catalog_t vr_weapon_menu_wwheel_catalog = {
	vr_weapon_menu_wwheel_entries, 0, 1
};
static qboolean vr_weapon_menu_has_wwheel;
static qboolean vr_weapon_menu_has_schema;
static qboolean vr_weapon_menu_has_profile;
static qboolean vr_weapon_menu_has_discoveries;

static qboolean vr_weapon_menu_open;
static qboolean vr_weapon_menu_open_vr;
static qboolean vr_weapon_menu_pointer_valid;
static qboolean vr_weapon_menu_tracking_valid;
static int vr_weapon_menu_pointer_x, vr_weapon_menu_pointer_y;
static int vr_weapon_menu_hover_id = -1;
static int vr_weapon_menu_retained_id = -1;
static int vr_weapon_menu_hover_action_slot = -1;
static char vr_weapon_menu_hover_action_name[MAX_SCOREBOARDNAME];
static qboolean vr_weapon_menu_frame_valid;
static unsigned int vr_weapon_menu_session_generation;
static qmodel_t *vr_weapon_menu_worldmodel;
static int vr_weapon_menu_viewentity;
static char vr_weapon_menu_mapname[sizeof (cl.mapname)];

static const vr_weapon_menu_catalog_t *VR_WeaponMenu_CurrentCatalog (void)
{
	return (vr_weapon_menu_has_wwheel || vr_weapon_menu_has_schema ||
		vr_weapon_menu_has_profile || vr_weapon_menu_has_discoveries) ?
		&vr_weapon_menu_wwheel_catalog :
		&vr_weapon_menu_stock_catalog;
}

static qboolean VR_WeaponMenu_CatalogHasID (
	const vr_weapon_menu_catalog_t *catalog, int id)
{
	if (catalog && catalog->entries)
		for (size_t i = 0; i < catalog->count; ++i)
			if (catalog->entries[i].id == id)
				return true;
	return false;
}

static int VR_WeaponMenu_AllocateStableID (
	const vr_weapon_menu_catalog_t *catalog, int preferred)
{
	long long candidate = preferred;

	if (candidate < 0x40000000LL || candidate > INT_MAX)
		candidate = 0x40000000LL;
	for (int attempt = 0; attempt <= VR_WEAPON_MENU_MAX_ENTRIES; ++attempt)
	{
		if (candidate > INT_MAX)
			candidate = 0x40000000LL;
		if (!VR_WeaponMenu_CatalogHasID (catalog, (int)candidate))
			return (int)candidate;
		++candidate;
	}
	return -1;
}

static qboolean VR_WeaponMenu_ParseInteger (const char *text, int minimum,
	int maximum, int *value)
{
	char *end;
	long parsed;

	if (!text || !*text || !value)
		return false;
	errno = 0;
	parsed = strtol (text, &end, 10);
	if (errno == ERANGE || end == text || *end || parsed < minimum ||
		parsed > maximum)
		return false;
	*value = (int)parsed;
	return true;
}

static qboolean VR_WeaponMenu_NextToken (const char **cursor, char *token,
	size_t token_size, qboolean *eof)
{
	const char *next;
	qboolean parse_error = false;

	next = COM_ParseExBuffer (*cursor, CPE_NOTRUNC, token, token_size,
		&parse_error);
	if (parse_error)
		return false;
	if (!next)
	{
		*eof = true;
		return true;
	}
	*cursor = next;
	*eof = false;
	return true;
}

static int VR_WeaponMenu_FindStockSelector (int weaponnum, int impulse)
{
	for (size_t i = 0; i < sizeof (vr_weapon_menu_stock_entries) /
		sizeof (vr_weapon_menu_stock_entries[0]); ++i)
		if (vr_weapon_menu_stock_entries[i].selector == weaponnum &&
			vr_weapon_menu_stock_entries[i].impulse == impulse)
			return (int)i;
	return -1;
}

static int VR_WeaponMenu_ProfileAmmoMaxStat (int ammo_stat);

static qboolean VR_WeaponMenu_WheelAmmo (int entvaroffs, int *ammo_stat,
	int *ammo_max)
{
	switch (entvaroffs)
	{
	case 216:
		*ammo_stat = STAT_SHELLS;
		*ammo_max = 100;
		return true;
	case 220:
		*ammo_stat = STAT_NAILS;
		*ammo_max = 200;
		return true;
	case 224:
		*ammo_stat = STAT_ROCKETS;
		*ammo_max = 100;
		return true;
	case 228:
		*ammo_stat = STAT_CELLS;
		*ammo_max = 100;
		return true;
	default:
		return false;
	}
}

static qboolean VR_WeaponMenu_GameDirIs (const char *name);

static qboolean VR_WeaponMenu_AddWWheelSlot (int weaponnum, int impulse,
	qboolean have_entvaroffs, int entvaroffs)
{
	vr_weapon_menu_entry_t *entry;
	const size_t index = vr_weapon_menu_wwheel_catalog.count;
	int stock_index, ammo_stat = -1, ammo_max = 0;

	/* Primary's narrowly identified rerelease Rogue final axe typo. */
	if (rogue && weaponnum == RIT_LAVA_NAILGUN && impulse == 1)
		weaponnum = RIT_AXE;
	if (weaponnum <= 0 || impulse <= 0 || impulse > 255 ||
		vr_weapon_menu_wwheel_catalog.count >= VR_WEAPON_MENU_MAX_ENTRIES)
		return false;
	for (size_t i = 0; i < vr_weapon_menu_wwheel_catalog.count; ++i)
		if (vr_weapon_menu_wwheel_entries[i].id == weaponnum)
			return false;

	entry = &vr_weapon_menu_wwheel_entries[index];
	stock_index = VR_WeaponMenu_FindStockSelector (weaponnum, impulse);
	if (stock_index >= 0)
		*entry = vr_weapon_menu_stock_entries[stock_index];
	else
		memset (entry, 0, sizeof (*entry));

	entry->id = weaponnum;
	entry->kind = VR_WEAPON_MENU_WEAPON;
	entry->source = VR_WEAPON_CATALOG_SOURCE_SCHEMA;
	entry->label = stock_index >= 0 ? vr_weapon_menu_stock_entries[stock_index].label :
		vr_weapon_menu_wwheel_labels[index];
	entry->selector = weaponnum;
	entry->impulse = impulse;
	entry->owned_stat = VR_WeaponMenu_GameDirIs ("enyo") ? STAT_VR_WEAPONS : STAT_ITEMS;
	entry->owned_mask = weaponnum;
	entry->active_stat = STAT_ACTIVEWEAPON;
	entry->active_mask = weaponnum;
	entry->ammo_stat = -1;
	entry->ammo_max = 0;
	entry->ammo_max_stat = -1;
	/* wwheel supplies selector/command, not these inferred descriptor halves. */
	entry->schema_fields = VR_SCHEMA_WHEEL_BITMASK | VR_SCHEMA_WHEEL_IMPULSE;

	if (have_entvaroffs && VR_WeaponMenu_WheelAmmo (entvaroffs, &ammo_stat, &ammo_max))
		entry->schema_fields |= VR_SCHEMA_WHEEL_AMMO_STAT;
	entry->ammo_stat = ammo_stat;
	entry->ammo_max = ammo_max;
	entry->ammo_max_stat = VR_WeaponMenu_ProfileAmmoMaxStat (ammo_stat);
	if (stock_index < 0)
		q_snprintf (vr_weapon_menu_wwheel_labels[index],
			sizeof (vr_weapon_menu_wwheel_labels[0]), "WEAPON %d", weaponnum);

	vr_weapon_menu_wwheel_catalog.count++;
	return true;
}

#define VR_WHEEL_OWNERSHIP_FIELDS (VR_SCHEMA_WHEEL_OWNED_STAT | VR_SCHEMA_WHEEL_OWNED_MASK)
#define VR_WHEEL_ACTIVE_FIELDS (VR_SCHEMA_WHEEL_ACTIVE_STAT | VR_SCHEMA_WHEEL_ACTIVE_MASK)
#define VR_WHEEL_AMMO_FIELDS (VR_SCHEMA_WHEEL_AMMO | VR_SCHEMA_WHEEL_AMMO_STAT)

typedef enum { VR_WHEEL_NOT_FOUND, VR_WHEEL_MATCH, VR_WHEEL_AMBIGUOUS } vr_wheel_match_t;

static vr_weapon_catalog_identity_t VR_WeaponMenu_EntryIdentity (
	const vr_weapon_menu_entry_t *entry)
{
	vr_weapon_catalog_identity_t identity = {
		entry->selector, entry->owned_stat, entry->owned_mask,
		entry->active_stat, entry->active_mask
	};
	if (identity.active_stat < 0 && identity.selector)
	{
		identity.active_stat = STAT_ACTIVEWEAPON;
		identity.active_mask = identity.selector;
	}
	if (!identity.selector && identity.active_stat == STAT_ACTIVEWEAPON)
		identity.selector = identity.active_mask;
	return identity;
}

static qboolean VR_WeaponMenu_SchemaCompatible (
	const vr_weapon_schema_wheel_t *schema, const vr_weapon_menu_entry_t *entry)
{
	const unsigned int fields = schema->fields;
	vr_weapon_catalog_identity_t known = VR_WeaponMenu_EntryIdentity (entry);
	vr_weapon_catalog_identity_t query = {
		(fields & VR_SCHEMA_WHEEL_BITMASK) ? schema->bitmask : 0,
		(fields & VR_WHEEL_OWNERSHIP_FIELDS) == VR_WHEEL_OWNERSHIP_FIELDS ? schema->owned_stat : -1,
		schema->owned_mask,
		(fields & VR_WHEEL_ACTIVE_FIELDS) == VR_WHEEL_ACTIVE_FIELDS ? schema->active_stat : -1,
		schema->active_mask
	};
	/* Half declarations constrain existing explicit descriptors, but cannot
	 * invent a relationship or fill their other half during candidate search. */
	if ((fields & VR_SCHEMA_WHEEL_OWNED_STAT) && schema->owned_stat >= 0 &&
		known.owned_stat >= 0 && (entry->game_profile || (entry->schema_fields & VR_SCHEMA_WHEEL_OWNED_STAT)) &&
		schema->owned_stat != known.owned_stat)
		return false;
	if ((fields & VR_SCHEMA_WHEEL_OWNED_MASK) &&
		((entry->schema_fields & VR_SCHEMA_WHEEL_OWNED_MASK) ||
		 (entry->game_profile && known.owned_stat >= 0 &&
		  (!(fields & VR_SCHEMA_WHEEL_OWNED_STAT) || schema->owned_stat >= 0))) &&
		schema->owned_mask != entry->owned_mask)
		return false;
	if ((fields & VR_SCHEMA_WHEEL_ACTIVE_STAT) && schema->active_stat >= 0 &&
		known.active_stat >= 0 && (entry->game_profile || (entry->schema_fields & VR_SCHEMA_WHEEL_ACTIVE_STAT)) &&
		schema->active_stat != known.active_stat)
		return false;
	if ((fields & VR_SCHEMA_WHEEL_ACTIVE_MASK) &&
		((entry->schema_fields & VR_SCHEMA_WHEEL_ACTIVE_MASK) ||
		 (entry->game_profile && known.active_stat >= 0 &&
		  (!(fields & VR_SCHEMA_WHEEL_ACTIVE_STAT) || schema->active_stat >= 0))) &&
		schema->active_mask != ((entry->schema_fields & VR_SCHEMA_WHEEL_ACTIVE_MASK) ?
			entry->active_mask : known.active_mask))
		return false;
	/* Stock defaults are replaceable through another known relationship.
	 * Explicit conflicts have already been rejected above. */
	if (!entry->game_profile && query.owned_stat >= 0 && known.owned_stat >= 0 &&
		(query.owned_stat != known.owned_stat || query.owned_mask != known.owned_mask))
		known.owned_stat = -1;
	if (!entry->game_profile && query.active_stat >= 0 && known.active_stat >= 0 &&
		(query.active_stat != known.active_stat || query.active_mask != known.active_mask))
		known.active_stat = -1;
	return VR_WeaponCatalog_IdentitiesCompatible (query, known);
}

static vr_wheel_match_t VR_WeaponMenu_FindSchemaSlot (
	const vr_weapon_schema_wheel_t *schema, size_t *index)
{
	int best_rank = -1;
	vr_wheel_match_t result = VR_WHEEL_NOT_FOUND;
	for (size_t i = 0; i < vr_weapon_menu_wwheel_catalog.count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = &vr_weapon_menu_wwheel_entries[i];
		if (entry->source == VR_WEAPON_CATALOG_SOURCE_DISCOVERED ||
			!VR_WeaponMenu_SchemaCompatible (schema, entry))
			continue;
		const qboolean held_exact = (schema->fields & VR_SCHEMA_WHEEL_VIEWMODEL) &&
			schema->viewmodel_path[0] && entry->viewmodel_path &&
			!q_strcasecmp (schema->viewmodel_path, entry->viewmodel_path);
		const qboolean preview_exact = (schema->fields & VR_SCHEMA_WHEEL_MODEL) &&
			schema->model_path[0] && entry->model_path &&
			!q_strcasecmp (schema->model_path, entry->model_path);
		/* Same primary ranking: held > preview, then declaration/profile/stock. */
		const int rank = (held_exact ? 2 : preview_exact ? 1 : 0) * 4 +
			(entry->source == VR_WEAPON_CATALOG_SOURCE_SCHEMA ? 3 : entry->game_profile ? 2 : 1);
		if (rank > best_rank)
		{
			*index = i;
			best_rank = rank;
			result = VR_WHEEL_MATCH;
		}
		else if (rank == best_rank)
			result = VR_WHEEL_AMBIGUOUS;
	}
	return result;
}

static int VR_WeaponMenu_DefaultAmmoMax (int ammo_stat)
{
	switch (ammo_stat)
	{
	case STAT_NAILS: return 200;
	case STAT_SHELLS: case STAT_ROCKETS: case STAT_CELLS: return 100;
	default: return 0;
	}
}

static void VR_WeaponMenu_SchemaLabel (char *label, size_t label_size,
	const char *model_path, int fallback_id)
{
	const char *base, *end;
	size_t length = 0;

	base = strrchr (model_path, '/');
	base = base ? base + 1 : model_path;
	if ((tolower ((unsigned char)base[0]) == 'g' ||
		tolower ((unsigned char)base[0]) == 'v') && base[1] == '_')
		base += 2;
	end = strchr (base, '.');
	if (!end)
		end = base + strlen (base);
	while (base < end && length + 1 < label_size)
	{
		const unsigned char c = (unsigned char)*base++;
		if (isalnum (c))
			label[length++] = (char)toupper (c);
		else if (length && label[length - 1] != ' ')
			label[length++] = ' ';
	}
	while (length && label[length - 1] == ' ')
		--length;
	label[length] = '\0';
	if (!length)
		q_snprintf (label, label_size, "WEAPON %d", fallback_id);
}

static void VR_WeaponMenu_ApplySchemaMetadata (
	vr_weapon_menu_entry_t *entry, size_t index,
	const vr_weapon_schema_wheel_t *schema)
{
	const unsigned int fields = schema->fields;
	if (fields & VR_SCHEMA_WHEEL_MODEL)
	{
		if (!(entry->schema_fields & VR_SCHEMA_WHEEL_VIEWMODEL) &&
			!entry->game_profile &&
			(!entry->model_path || q_strcasecmp (entry->model_path, schema->model_path)))
			entry->viewmodel_path = NULL;
		q_strlcpy (vr_weapon_menu_schema_models[index], schema->model_path,
			sizeof (vr_weapon_menu_schema_models[index]));
		entry->model_path = vr_weapon_menu_schema_models[index];
		if (entry->label == vr_weapon_menu_wwheel_labels[index])
			VR_WeaponMenu_SchemaLabel (vr_weapon_menu_wwheel_labels[index],
				sizeof (vr_weapon_menu_wwheel_labels[index]), schema->model_path, entry->id);
	}
	if (fields & VR_SCHEMA_WHEEL_VIEWMODEL)
	{
		q_strlcpy (vr_weapon_menu_held_models[index], schema->viewmodel_path,
			sizeof (vr_weapon_menu_held_models[index]));
		entry->viewmodel_path = vr_weapon_menu_held_models[index];
		if (!entry->game_profile && !((entry->schema_fields | fields) & VR_SCHEMA_WHEEL_MODEL))
		{
			entry->model_path = entry->viewmodel_path;
		}
	}
	if (fields & VR_SCHEMA_WHEEL_BITMASK)
		entry->selector = schema->bitmask;
	if (fields & VR_SCHEMA_WHEEL_IMPULSE)
		entry->impulse = schema->impulse;
	if ((fields & VR_SCHEMA_WHEEL_SCALE) && isfinite (schema->scale) && schema->scale > 0.0f)
		entry->model_scale = schema->scale;
	if (fields & VR_SCHEMA_WHEEL_OFFSET)
		VectorCopy (schema->offset, entry->model_offset);
	if (fields & VR_SCHEMA_WHEEL_OWNED_STAT)
		entry->owned_stat = schema->owned_stat;
	if (fields & VR_SCHEMA_WHEEL_OWNED_MASK)
		entry->owned_mask = schema->owned_mask;
	if (fields & VR_WHEEL_OWNERSHIP_FIELDS)
		vr_weapon_menu_schema_bitmasks[index] = 0;
	if (fields & VR_SCHEMA_WHEEL_ACTIVE_STAT)
		entry->active_stat = schema->active_stat;
	if (fields & VR_SCHEMA_WHEEL_ACTIVE_MASK)
		entry->active_mask = schema->active_mask;
	if (fields & VR_WHEEL_AMMO_FIELDS)
	{
		if (schema->ammo_stat != entry->ammo_stat &&
			!((entry->schema_fields | fields) & VR_SCHEMA_WHEEL_AMMO_MAX))
			entry->ammo_max = VR_WeaponMenu_DefaultAmmoMax (schema->ammo_stat);
		entry->ammo_stat = schema->ammo_stat;
	}
	if (fields & VR_SCHEMA_WHEEL_AMMO_MAX)
		entry->ammo_max = schema->ammo_max;
	entry->schema_fields |= fields;
	entry->ammo_max_stat = VR_WeaponMenu_ProfileAmmoMaxStat (entry->ammo_stat);
	entry->source = VR_WEAPON_CATALOG_SOURCE_SCHEMA;
}

static qboolean VR_WeaponMenu_AddSchemaEntry (
	const vr_weapon_schema_wheel_t *schema)
{
	size_t index = 0;
	const vr_wheel_match_t match = VR_WeaponMenu_FindSchemaSlot (schema, &index);
	const unsigned int fields = schema->fields;
	if ((fields & VR_SCHEMA_WHEEL_IMPULSE) && (schema->impulse < 0 || schema->impulse > 255))
	{
		Con_Printf ("VR: invalid weapon declaration impulse %d\n", schema->impulse);
		return false;
	}
	if (match == VR_WHEEL_AMBIGUOUS)
	{
		Con_Printf ("VR: ambiguous weapon declaration selector %d; declare ownership and held model\n", schema->bitmask);
		return false;
	}
	if (match == VR_WHEEL_NOT_FOUND)
	{
		const qboolean ownership = (fields & VR_WHEEL_OWNERSHIP_FIELDS) == VR_WHEEL_OWNERSHIP_FIELDS;
		const qboolean activation = (fields & VR_WHEEL_ACTIVE_FIELDS) == VR_WHEEL_ACTIVE_FIELDS;
		/* Incomplete/calibration-only declarations may enrich a unique slot,
		 * but cannot authorize a guessed command, descriptor half or row. */
		if (!(fields & VR_SCHEMA_WHEEL_IMPULSE) || schema->impulse <= 0 ||
			(!((fields & VR_SCHEMA_WHEEL_BITMASK) && schema->bitmask) &&
			 !(ownership && schema->owned_stat >= 0) && !(activation && schema->active_stat >= 0)) ||
			((fields & VR_WHEEL_OWNERSHIP_FIELDS) && !ownership) ||
			((fields & VR_WHEEL_ACTIVE_FIELDS) && !activation) ||
			vr_weapon_menu_wwheel_catalog.count >= VR_WEAPON_MENU_MAX_ENTRIES)
			return false;
		const int id = VR_WeaponMenu_AllocateStableID (&vr_weapon_menu_wwheel_catalog,
			0x40000000 + (int)vr_weapon_menu_wwheel_catalog.count);
		if (id < 0)
			return false;
		index = vr_weapon_menu_wwheel_catalog.count++;
		vr_weapon_menu_entry_t *entry = &vr_weapon_menu_wwheel_entries[index];
		memset (entry, 0, sizeof (*entry));
		entry->id = id;
		entry->kind = VR_WEAPON_MENU_WEAPON;
		entry->label = vr_weapon_menu_wwheel_labels[index];
		entry->model_scale = 1.0f;
		entry->owned_stat = entry->active_stat = entry->ammo_stat = entry->ammo_max_stat = -1;
		if ((fields & VR_SCHEMA_WHEEL_BITMASK) && schema->bitmask && !ownership)
		{
			entry->owned_stat = STAT_VR_WEAPONS;
			entry->owned_mask = schema->bitmask;
			vr_weapon_menu_schema_bitmasks[index] = schema->bitmask;
		}
		if (!((fields & VR_SCHEMA_WHEEL_BITMASK) && schema->bitmask) &&
			activation && schema->active_stat == STAT_ACTIVEWEAPON)
			entry->selector = schema->active_mask;
		q_snprintf (vr_weapon_menu_wwheel_labels[index],
			sizeof (vr_weapon_menu_wwheel_labels[index]), "WEAPON %d", id);
	}
	VR_WeaponMenu_ApplySchemaMetadata (&vr_weapon_menu_wwheel_entries[index], index, schema);
	return true;
}

static qboolean VR_WeaponMenu_LoadSchema (
	vr_weapon_schema_entry_t *entries, size_t *count,
	vr_weapon_schema_metadata_t *metadata)
{
	byte *data;
	unsigned int path_id = 0;
	qboolean parsed;

	*count = 0;
	memset (metadata, 0, sizeof (*metadata));
	data = COM_LoadFile ("vr_weapons.txt", &path_id);
	if (!data)
		return false;
	if (!com_searchpaths || path_id != com_searchpaths->path_id)
	{
		Con_DPrintf ("VR: ignoring inherited vr_weapons.txt for %s\n",
			com_gamedir);
		Mem_Free (data);
		return false;
	}
	parsed = VR_WeaponSchemaParseWithMetadata ((const char *)data, entries,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, count, metadata);
	Mem_Free (data);
	if (!parsed || (!*count && !metadata->complete_roster))
	{
		*count = 0;
		Con_DPrintf ("VR: ignoring invalid or empty vr_weapons.txt for %s\n",
			com_gamedir);
		return false;
	}
	return true;
}

static void VR_WeaponMenu_ApplySchema (
	const vr_weapon_schema_entry_t *schemas, size_t schema_count)
{
	for (size_t s = 0; s < schema_count; ++s)
		if (VR_WeaponMenu_AddSchemaEntry (&schemas[s].wheel))
			vr_weapon_menu_has_schema = true;
}

static qboolean VR_WeaponMenu_GameDirIs (const char *name)
{
	const char *game = COM_SkipPath (com_gamedir);
	return game && name && !q_strcasecmp (game, name);
}

static int VR_WeaponMenu_ProfileAmmoMaxStat (int ammo_stat)
{
	switch (ammo_stat)
	{
	case STAT_SHELLS: return STAT_VR_MAX_SHELLS;
	case STAT_NAILS: return STAT_VR_MAX_NAILS;
	case STAT_ROCKETS: return STAT_VR_MAX_ROCKETS;
	case STAT_CELLS: return STAT_VR_MAX_CELLS;
	default: return -1;
	}
}

static int VR_WeaponMenu_ProfileId (void)
{
	const int preferred = 0x40000000 +
		(int)vr_weapon_menu_wwheel_catalog.count;
	return VR_WeaponMenu_AllocateStableID (&vr_weapon_menu_wwheel_catalog,
		preferred);
}

static void VR_WeaponMenu_SeedStock (void)
{
	for (size_t i = 0; i < vr_weapon_menu_stock_catalog.count; ++i)
	{
		vr_weapon_menu_wwheel_entries[i] = vr_weapon_menu_stock_entries[i];
	}
	vr_weapon_menu_wwheel_catalog.count = vr_weapon_menu_stock_catalog.count;
	vr_weapon_menu_wwheel_catalog.authoritative_schema = 0;
}

static qboolean VR_WeaponMenu_AddProfileEntry (
	const vr_weapon_menu_profile_entry_t *profile)
{
	if (!profile || profile->selector <= 0 || profile->impulse <= 0 || profile->impulse > 255)
		return false;
	vr_weapon_schema_wheel_t query = {0};
	query.fields = VR_SCHEMA_WHEEL_BITMASK | VR_SCHEMA_WHEEL_MODEL;
	query.bitmask = profile->selector;
	query.owned_stat = profile->owned_stat;
	query.owned_mask = profile->owned_mask;
	query.active_stat = profile->active_stat;
	query.active_mask = profile->active_mask;
	if (profile->owned_stat >= 0)
		query.fields |= VR_WHEEL_OWNERSHIP_FIELDS;
	if (profile->active_stat >= 0)
		query.fields |= VR_WHEEL_ACTIVE_FIELDS;
	q_strlcpy (query.model_path, profile->model_path, sizeof (query.model_path));
	size_t index = 0;
	vr_wheel_match_t match = VR_WeaponMenu_FindSchemaSlot (&query, &index);
	if (match == VR_WHEEL_NOT_FOUND && profile->replace_selector)
	{
		query.bitmask = profile->replace_selector;
		match = VR_WeaponMenu_FindSchemaSlot (&query, &index);
		if (match == VR_WHEEL_MATCH &&
			vr_weapon_menu_wwheel_entries[index].source != VR_WEAPON_CATALOG_SOURCE_STOCK)
			match = VR_WHEEL_NOT_FOUND;
	}
	if (match == VR_WHEEL_AMBIGUOUS)
	{
		Con_Printf ("VR: ambiguous native weapon profile selector %d\n", profile->selector);
		return false;
	}
	vr_weapon_menu_entry_t *entry;
	if (match == VR_WHEEL_NOT_FOUND)
	{
		if (vr_weapon_menu_wwheel_catalog.count >= VR_WEAPON_MENU_MAX_ENTRIES)
			return false;
		const int id = VR_WeaponMenu_ProfileId ();
		if (id < 0)
			return false;
		index = vr_weapon_menu_wwheel_catalog.count++;
		entry = &vr_weapon_menu_wwheel_entries[index];
		memset (entry, 0, sizeof (*entry));
		entry->id = id;
	}
	else
	{
		entry = &vr_weapon_menu_wwheel_entries[index];
		if (entry->source == VR_WEAPON_CATALOG_SOURCE_STOCK)
		{
			/* Deliberate native fallback replacement keeps stable selection ID.
			 * Keep a known held alias only for an unchanged native preview. */
			const int id = entry->id;
			const char *preview = entry->model_path;
			const char *held = preview &&
				!q_strcasecmp (entry->model_path, profile->model_path) ? entry->viewmodel_path : NULL;
			memset (entry, 0, sizeof (*entry));
			entry->id = id;
			entry->model_path = preview;
			entry->viewmodel_path = held;
		}
	}
	const unsigned int fields = entry->schema_fields;
	entry->kind = VR_WEAPON_MENU_WEAPON;
	entry->label = profile->label;
	if (entry->source != VR_WEAPON_CATALOG_SOURCE_SCHEMA)
		entry->source = VR_WEAPON_CATALOG_SOURCE_PROFILE;
	if (!(fields & VR_SCHEMA_WHEEL_MODEL))
	{
		if (!(fields & VR_SCHEMA_WHEEL_VIEWMODEL) &&
			(!entry->model_path || q_strcasecmp (entry->model_path, profile->model_path)))
			entry->viewmodel_path = NULL;
		entry->model_path = profile->model_path;
	}
	if (!(fields & VR_SCHEMA_WHEEL_SCALE))
		entry->model_scale = 1.0f;
	if (!(fields & VR_SCHEMA_WHEEL_BITMASK))
		entry->selector = profile->selector;
	if (!(fields & VR_SCHEMA_WHEEL_IMPULSE))
		entry->impulse = profile->impulse;
	/* A profile's missing pair does not erase wwheel's generic fallback. */
	if (profile->owned_stat >= 0 || entry->source != VR_WEAPON_CATALOG_SOURCE_SCHEMA)
	{
		if (!(fields & VR_SCHEMA_WHEEL_OWNED_STAT))
			entry->owned_stat = profile->owned_stat;
		if (!(fields & VR_SCHEMA_WHEEL_OWNED_MASK))
			entry->owned_mask = profile->owned_mask;
	}
	if (profile->active_stat >= 0 || entry->source != VR_WEAPON_CATALOG_SOURCE_SCHEMA)
	{
		if (!(fields & VR_SCHEMA_WHEEL_ACTIVE_STAT))
			entry->active_stat = profile->active_stat;
		if (!(fields & VR_SCHEMA_WHEEL_ACTIVE_MASK))
			entry->active_mask = profile->active_mask;
	}
	if (!(fields & VR_WHEEL_AMMO_FIELDS))
		entry->ammo_stat = profile->ammo_stat;
	if (!(fields & VR_SCHEMA_WHEEL_AMMO_MAX) && entry->ammo_stat == profile->ammo_stat)
		entry->ammo_max = profile->ammo_max;
	entry->ammo_max_stat = VR_WeaponMenu_ProfileAmmoMaxStat (entry->ammo_stat);
	if (!(fields & VR_SCHEMA_WHEEL_VIEWMODEL))
	{
		entry->viewmodel_path = profile->viewmodel_path;
		if (!entry->viewmodel_path)
		{
			/* Primary's native fallback derives held from the profile's pickup,
			 * independently of an authored wheel preview. */
			q_strlcpy (vr_weapon_menu_held_models[index], profile->model_path,
				sizeof (vr_weapon_menu_held_models[index]));
			char *pickup = strstr (vr_weapon_menu_held_models[index], "/g_");
			if (pickup)
				pickup[1] = 'v';
			entry->viewmodel_path = vr_weapon_menu_held_models[index];
		}
	}
	entry->game_profile = true;
	vr_weapon_menu_has_profile = true;
	return true;
}

static void VR_WeaponMenu_AddProfile (
	const vr_weapon_menu_profile_entry_t *profile, size_t count)
{
	for (size_t i = 0; i < count; ++i)
		VR_WeaponMenu_AddProfileEntry (&profile[i]);
}

static void VR_WeaponMenu_LoadBuiltinProfiles (void)
{
	if (VR_WeaponMenu_GameDirIs ("ad"))
		VR_WeaponMenu_AddProfile (vr_weapon_menu_ad_profile,
			sizeof (vr_weapon_menu_ad_profile) / sizeof (vr_weapon_menu_ad_profile[0]));
	if (VR_WeaponMenu_GameDirIs ("dwell") ||
		VR_WeaponMenu_GameDirIs ("dwellv2p2"))
		VR_WeaponMenu_AddProfile (vr_weapon_menu_dwell_profile,
			sizeof (vr_weapon_menu_dwell_profile) /
			sizeof (vr_weapon_menu_dwell_profile[0]));
	if (VR_WeaponMenu_GameDirIs ("alk") || VR_WeaponMenu_GameDirIs ("limjam"))
		VR_WeaponMenu_AddProfile (vr_weapon_menu_alkaline_profile,
			sizeof (vr_weapon_menu_alkaline_profile) /
			sizeof (vr_weapon_menu_alkaline_profile[0]));
	if (VR_WeaponMenu_GameDirIs ("enyo"))
		VR_WeaponMenu_AddProfile (vr_weapon_menu_enyo_profile,
			sizeof (vr_weapon_menu_enyo_profile) /
			sizeof (vr_weapon_menu_enyo_profile[0]));
	if (VR_WeaponMenu_GameDirIs ("qbj3"))
		VR_WeaponMenu_AddProfile (vr_weapon_menu_qbj3_profile,
			sizeof (vr_weapon_menu_qbj3_profile) /
			sizeof (vr_weapon_menu_qbj3_profile[0]));
	if (VR_WeaponMenu_GameDirIs ("mjolnir") ||
		VR_WeaponMenu_GameDirIs ("mjolnir1.0"))
		VR_WeaponMenu_AddProfile (vr_weapon_menu_mjolnir_profile,
			sizeof (vr_weapon_menu_mjolnir_profile) /
			sizeof (vr_weapon_menu_mjolnir_profile[0]));
	if (VR_WeaponMenu_GameDirIs ("mg3"))
		VR_WeaponMenu_AddProfile (vr_weapon_menu_mg3_profile,
			sizeof (vr_weapon_menu_mg3_profile) /
			sizeof (vr_weapon_menu_mg3_profile[0]));
	if (hipnotic)
		VR_WeaponMenu_AddProfile (vr_weapon_menu_hipnotic_profile,
			sizeof (vr_weapon_menu_hipnotic_profile) /
			sizeof (vr_weapon_menu_hipnotic_profile[0]));
	if (rogue)
		VR_WeaponMenu_AddProfile (vr_weapon_menu_rogue_profile,
			sizeof (vr_weapon_menu_rogue_profile) /
			sizeof (vr_weapon_menu_rogue_profile[0]));
	/* Only the primary's verified complete families suppress stock guesses. */
	if (VR_WeaponMenu_GameDirIs ("ad") || VR_WeaponMenu_GameDirIs ("alk") ||
		VR_WeaponMenu_GameDirIs ("limjam") || VR_WeaponMenu_GameDirIs ("enyo") ||
		VR_WeaponMenu_GameDirIs ("qbj3") || VR_WeaponMenu_GameDirIs ("dwell") ||
		VR_WeaponMenu_GameDirIs ("dwellv2p2"))
		vr_weapon_menu_wwheel_catalog.authoritative_schema = 1;
}

static qboolean VR_WeaponMenu_ParseWWheel (const char *data)
{
	const char *cursor = data;
	char token[64];
	qboolean eof = false;

	while (!eof)
	{
		int weaponnum = 0, impulse = 0, entvaroffs = 0;
		qboolean have_weaponnum = false, have_impulse = false;
		qboolean have_entvaroffs = false, slot_valid = true;

		if (!VR_WeaponMenu_NextToken (&cursor, token, sizeof (token), &eof))
			return false;
		if (eof)
			break;
		if (q_strcasecmp (token, "slot"))
			continue;

		if (!VR_WeaponMenu_NextToken (&cursor, token, sizeof (token), &eof) || eof)
			return false;
		if (strcmp (token, "{"))
		{
			/* Inherited files commonly spell this as `slot 0 {`; accept a
			 * direct brace too, but require the identifier to be followed by it. */
			if (!strcmp (token, "}"))
				return false;
			if (!VR_WeaponMenu_NextToken (&cursor, token, sizeof (token), &eof) ||
				eof || strcmp (token, "{"))
				return false;
		}

		for (;;)
		{
			if (!VR_WeaponMenu_NextToken (&cursor, token, sizeof (token), &eof))
				return false;
			if (eof)
				return false;
			if (!strcmp (token, "}"))
				break;
			if (!strcmp (token, "{") || !q_strcasecmp (token, "slot"))
				return false;

			if (!q_strcasecmp (token, "weaponnum") ||
				!q_strcasecmp (token, "weapon_num"))
			{
				if (!VR_WeaponMenu_NextToken (&cursor, token, sizeof (token), &eof) || eof)
					return false;
				have_weaponnum = VR_WeaponMenu_ParseInteger (token, 1, INT_MAX,
					&weaponnum);
				if (!have_weaponnum)
					slot_valid = false;
			}
			else if (!q_strcasecmp (token, "impulse"))
			{
				if (!VR_WeaponMenu_NextToken (&cursor, token, sizeof (token), &eof) || eof)
					return false;
				have_impulse = VR_WeaponMenu_ParseInteger (token, 1, 255, &impulse);
				if (!have_impulse)
					slot_valid = false;
			}
			else if (!q_strcasecmp (token, "entvaroffs"))
			{
				if (!VR_WeaponMenu_NextToken (&cursor, token, sizeof (token), &eof) || eof)
					return false;
				have_entvaroffs = VR_WeaponMenu_ParseInteger (token, INT_MIN,
					INT_MAX, &entvaroffs);
				if (!have_entvaroffs)
					slot_valid = false;
			}
			else
			{
				/* The inherited file may carry extra scalar fields. Consume their
				 * values, but reject a missing value or a structural token. */
				if (!VR_WeaponMenu_NextToken (&cursor, token, sizeof (token), &eof) ||
					eof || !strcmp (token, "{") || !strcmp (token, "}"))
					return false;
			}
		}

		if (slot_valid && have_weaponnum && have_impulse)
			VR_WeaponMenu_AddWWheelSlot (weaponnum, impulse,
				have_entvaroffs, entvaroffs);
	}

	return vr_weapon_menu_wwheel_catalog.count > 0;
}

void VR_WeaponMenu_ReloadGame (void)
{
	byte *data;
	unsigned int path_id = 0;
	vr_weapon_schema_entry_t schema_entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	vr_weapon_schema_metadata_t schema_metadata;
	size_t schema_count = 0;

	VR_WeaponMenu_ClientReset ();
	memset (vr_weapon_menu_wwheel_entries, 0,
		sizeof (vr_weapon_menu_wwheel_entries));
	memset (vr_weapon_menu_wwheel_labels, 0,
		sizeof (vr_weapon_menu_wwheel_labels));
	memset (vr_weapon_menu_schema_models, 0,
		sizeof (vr_weapon_menu_schema_models));
	memset (vr_weapon_menu_runtime_models, 0,
		sizeof (vr_weapon_menu_runtime_models));
	memset (vr_weapon_menu_held_models, 0,
		sizeof (vr_weapon_menu_held_models));
	memset (vr_weapon_menu_schema_bitmasks, 0,
		sizeof (vr_weapon_menu_schema_bitmasks));
	vr_weapon_menu_wwheel_catalog.count = 0;
	vr_weapon_menu_wwheel_catalog.authoritative_schema = 0;
	vr_weapon_menu_has_wwheel = false;
	vr_weapon_menu_has_schema = false;
	vr_weapon_menu_has_profile = false;
	vr_weapon_menu_has_discoveries = false;

	data = COM_LoadFile ("wwheel.txt", &path_id);
	if (data && (!com_searchpaths || path_id != com_searchpaths->path_id))
	{
		Con_DPrintf ("VR: ignoring inherited wwheel.txt for %s\n", com_gamedir);
		Mem_Free (data);
		data = NULL;
	}
	if (data && VR_WeaponMenu_ParseWWheel ((const char *)data))
	{
		vr_weapon_menu_has_wwheel = true;
		vr_weapon_menu_wwheel_catalog.authoritative_schema = 1;
		Con_DPrintf ("VR: loaded %d weapon slots from wwheel.txt\n",
			(int)vr_weapon_menu_wwheel_catalog.count);
	}
	else if (data)
	{
		memset (vr_weapon_menu_wwheel_entries, 0,
			sizeof (vr_weapon_menu_wwheel_entries));
		memset (vr_weapon_menu_wwheel_labels, 0,
			sizeof (vr_weapon_menu_wwheel_labels));
		vr_weapon_menu_wwheel_catalog.count = 0;
		Con_DPrintf ("VR: ignoring invalid or empty wwheel.txt for %s\n",
			com_gamedir);
	}
	if (data)
		Mem_Free (data);

	if (!vr_weapon_menu_has_wwheel)
		VR_WeaponMenu_SeedStock ();
	VR_WeaponMenu_LoadBuiltinProfiles ();
	if (VR_WeaponMenu_LoadSchema (schema_entries, &schema_count, &schema_metadata))
	{
		VR_WeaponMenu_ApplySchema (schema_entries, schema_count);
		if (schema_metadata.complete_roster)
		{
			/* Even an empty complete declaration suppresses stock guesses. */
			vr_weapon_menu_has_schema = true;
			vr_weapon_menu_wwheel_catalog.authoritative_schema = 1;
		}
	}
}

static qboolean VR_WeaponMenu_GameContextValid (void)
{
	return key_dest == key_game &&
		!con_forcedup && cls.state == ca_connected && cl.intermission == 0 &&
		cl.worldmodel != NULL && vid.width > 0 && vid.height > 0 &&
		glwidth > 0 && glheight > 0;
}

static qboolean VR_WeaponMenu_XRContextValid (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const int dominant = VR_InputDominantPhysicalHand ();
	const vrxr_device_t *head, *hand;
	if (!vulkan_globals.stereo_active || !frame || !frame->should_render ||
		!frame->focused || frame->reference_changed || dominant < 0 || dominant > 1)
		return false;
	head = &frame->devices[0];
	hand = &frame->devices[dominant + 1];
	return head->valid && head->tracked && head->kind == VRXR_DEVICE_HEAD && head->hand == -1 &&
		hand->valid && hand->tracked && hand->kind == VRXR_DEVICE_HAND && hand->hand == dominant;
}

static qboolean VR_WeaponMenu_SessionValid (void)
{
	if (!VR_WeaponMenu_GameContextValid () ||
		(vr_weapon_menu_open_vr ? !VR_WeaponMenu_XRContextValid () : vulkan_globals.stereo_active))
		return false;
	return vr_weapon_menu_worldmodel == cl.worldmodel &&
		vr_weapon_menu_viewentity == cl.viewentity &&
		q_strcasecmp (vr_weapon_menu_mapname, cl.mapname) == 0;
}

static void VR_WeaponMenu_ClearSession (void)
{
	vr_weapon_menu_open = false;
	vr_weapon_menu_open_vr = false;
	vr_weapon_menu_pointer_valid = false;
	vr_weapon_menu_tracking_valid = false;
	vr_weapon_menu_pointer_x = vr_weapon_menu_pointer_y = -1;
	vr_weapon_menu_hover_id = -1;
	vr_weapon_menu_retained_id = -1;
	vr_weapon_menu_hover_action_slot = -1;
	vr_weapon_menu_hover_action_name[0] = '\0';
	vr_weapon_menu_frame_valid = false;
	vr_weapon_menu_worldmodel = NULL;
	vr_weapon_menu_viewentity = 0;
	vr_weapon_menu_mapname[0] = '\0';
}

static int VR_WeaponMenu_Stat (const int *stats, size_t num_stats, int stat)
{
	if (!stats || stat < 0 || (size_t)stat >= num_stats)
		return 0;
	return stats[stat];
}

static qboolean VR_WeaponMenu_ModelPathLooksWeapon (const char *path)
{
	const char *base;
	static const char *const blocked[] = {
		"player", "rune", "sigil", "key", "armor", "health",
		"backpack", "gib", "head", "corpse", NULL
	};
	static const char *const projectile_blocked[] = {
		"spike", "vore", "lavaball", "fireball", "proj", "zgrenade",
		"trsh", NULL
	};

	if (!path || !path[0])
		return false;
	base = COM_SkipPath (path);
	for (int i = 0; blocked[i]; ++i)
		if (q_strcasestr (base, blocked[i]))
			return false;
	for (int i = 0; projectile_blocked[i]; ++i)
		if (q_strcasestr (base, projectile_blocked[i]))
			return false;
	if ((tolower ((unsigned char)base[0]) == 'v' ||
		tolower ((unsigned char)base[0]) == 'g') && base[1] == '_')
		return true;
	return q_strcasestr (base, "weapon") || q_strcasestr (base, "gun") ||
		q_strcasestr (base, "shot") || q_strcasestr (base, "rifle") ||
		q_strcasestr (base, "pistol") || q_strcasestr (base, "launcher") ||
		q_strcasestr (base, "wrench") || q_strcasestr (base, "hammer");
}

static const char *VR_WeaponMenu_CurrentModelPath (
	const int *stats, size_t num_stats)
{
	const int model_index = VR_WeaponMenu_Stat (stats, num_stats, STAT_WEAPON);
	qmodel_t *model;

	if (cls.state != ca_connected || model_index <= 0 ||
		model_index >= MAX_MODELS)
		return NULL;
	model = cl.model_precache[model_index];
	if (!model || !model->name[0])
		return NULL;
	return model->name;
}

static qboolean VR_WeaponMenu_EntryMatchesSelector (
	const vr_weapon_menu_entry_t *entry, int selector)
{
	return selector != 0 &&
		(entry->selector == selector ||
		 (entry->active_stat == STAT_ACTIVEWEAPON &&
		  entry->active_mask == selector));
}

static qboolean VR_WeaponMenu_ProfileUsesItemOwnership (int selector)
{
	if (selector == IT_SHOTGUN || selector == IT_SUPER_SHOTGUN ||
		selector == IT_NAILGUN || selector == IT_SUPER_NAILGUN ||
		selector == IT_GRENADE_LAUNCHER || selector == IT_ROCKET_LAUNCHER ||
		selector == IT_LIGHTNING)
		return true;
	if (!rogue && selector == IT_AXE)
		return true;
	if (rogue && (selector == RIT_AXE || selector == RIT_LAVA_NAILGUN ||
		selector == RIT_LAVA_SUPER_NAILGUN || selector == RIT_MULTI_GRENADE ||
		selector == RIT_MULTI_ROCKET || selector == RIT_PLASMA_GUN))
		return true;
	if (hipnotic && (selector == HIT_MJOLNIR || selector == HIT_LASER_CANNON ||
		selector == HIT_PROXIMITY_GUN))
		return true;
	return false;
}

/* Port of primary VR_DynWeaponViewmodel/PreviewPath: upgrades keep their native
 * parent slot. Authored held and preview paths have independent precedence. */
static const char *VR_WeaponMenu_EntryViewmodel (const vr_weapon_menu_entry_t *entry,
	const int *stats, size_t num_stats)
{
	if (entry->game_profile && !(entry->schema_fields & VR_SCHEMA_WHEEL_VIEWMODEL))
	{
		if (VR_WeaponMenu_GameDirIs ("enyo") && entry->selector == 4)
			return (VR_WeaponMenu_Stat (stats, num_stats, STAT_VR_WEAPONS) & 16384) ?
				"progs/ee_v_av72.mdl" : "progs/ee_v_smgs.mdl";
		if (VR_WeaponMenu_GameDirIs ("ad"))
		{
			const int modifiers = VR_WeaponMenu_Stat (stats, num_stats, STAT_VR_MODITEMS);
			switch (entry->selector)
			{
			case IT_AXE: return (modifiers & 128) ? "progs/v_ghook.mdl" :
				(modifiers & 4096) ? "progs/v_shadaxe3.mdl" : "progs/v_shadaxe0.mdl";
			case IT_SUPER_SHOTGUN: return (modifiers & 2) ? "progs/v_shot3.mdl" : "progs/v_shot2.mdl";
			case IT_LIGHTNING: return (modifiers & 64) ? "progs/v_plasma.mdl" : "progs/v_light.mdl";
			}
		}
	}
	return entry->viewmodel_path;
}

static const char *VR_WeaponMenu_EntryPreviewPath (const vr_weapon_menu_entry_t *entry,
	const int *stats, size_t num_stats)
{
	if (entry->game_profile && !(entry->schema_fields & VR_SCHEMA_WHEEL_MODEL))
	{
		if (VR_WeaponMenu_GameDirIs ("enyo") && entry->selector == 4)
			return (VR_WeaponMenu_Stat (stats, num_stats, STAT_VR_WEAPONS) & 16384) ?
				"progs/ee_g_av72.mdl" : "progs/ee_g_smgs.mdl";
		if (VR_WeaponMenu_GameDirIs ("ad"))
		{
			const int modifiers = VR_WeaponMenu_Stat (stats, num_stats, STAT_VR_MODITEMS);
			switch (entry->selector)
			{
			case IT_AXE: return (modifiers & 128) ? "progs/g_ghook.mdl" :
				(modifiers & 4096) ? "progs/g_shadaxe.mdl" : "progs/g_axe.mdl";
			case IT_SUPER_SHOTGUN: return (modifiers & 2) ? "progs/g_shot3.mdl" : "progs/g_shot2.mdl";
			case IT_LIGHTNING: return (modifiers & 64) ? "progs/g_plasma.mdl" : "progs/g_light.mdl";
			}
		}
	}
	return entry->model_path;
}

static qboolean VR_WeaponMenu_EntryModelMatches (
	const vr_weapon_menu_entry_t *entry, const char *model_path,
	const int *stats, size_t num_stats)
{
	if (model_path && entry->game_profile && !(entry->schema_fields & VR_SCHEMA_WHEEL_VIEWMODEL))
	{
		if (VR_WeaponMenu_GameDirIs ("enyo") && entry->selector == 4 &&
			(!q_strcasecmp (model_path, "progs/ee_v_smgs.mdl") ||
			 !q_strcasecmp (model_path, "progs/ee_v_av72.mdl")))
			return true;
		if (VR_WeaponMenu_GameDirIs ("ad") &&
			((entry->selector == IT_AXE && !q_strcasecmp (model_path, "progs/v_ghook.mdl")) ||
			 (entry->selector == IT_SUPER_SHOTGUN &&
			  (!q_strcasecmp (model_path, "progs/v_shot2.mdl") || !q_strcasecmp (model_path, "progs/v_shot3.mdl"))) ||
			 (entry->selector == IT_LIGHTNING &&
			  (!q_strcasecmp (model_path, "progs/v_light.mdl") || !q_strcasecmp (model_path, "progs/v_plasma.mdl")))))
			return true;
		if (VR_WeaponMenu_GameDirIs ("ad") && entry->selector == IT_AXE &&
			!q_strncasecmp (model_path, "progs/v_shadaxe", 15) &&
			model_path[15] >= '0' && model_path[15] <= '5' && !q_strcasecmp (model_path + 16, ".mdl"))
			return true;
		if ((VR_WeaponMenu_GameDirIs ("dwell") || VR_WeaponMenu_GameDirIs ("dwellv2p2")) &&
			((entry->selector == IT_AXE && !q_strcasecmp (model_path, "progs/v_axeb.mdl")) ||
			 (entry->selector == IT_SUPER_NAILGUN && !q_strcasecmp (model_path, "progs/v_nail3.mdl"))))
			return true;
	}
	const char *held = VR_WeaponMenu_EntryViewmodel (entry, stats, num_stats);
	if (held && held[0])
		return model_path && (!q_strcasecmp (held, model_path) ||
			(entry->model_path && !q_strcasecmp (entry->model_path, model_path)));
	return VR_WeaponCatalog_ModelPathsMatch (entry->model_path, model_path);
}

static qboolean VR_WeaponMenu_EntryStatActive (const vr_weapon_menu_entry_t *entry,
	const int *stats, size_t num_stats)
{
	if (entry->active_stat >= 0)
	{
		const int stat = VR_WeaponMenu_Stat (stats, num_stats, entry->active_stat);
		return entry->active_mask ? (stat & entry->active_mask) != 0 : stat != 0;
	}
	return entry->selector != 0 &&
		VR_WeaponMenu_Stat (stats, num_stats, STAT_ACTIVEWEAPON) == entry->selector;
}

static qboolean VR_WeaponMenu_EntryActive (const vr_weapon_menu_entry_t *entry,
	const int *stats, size_t num_stats)
{
	const qboolean active = VR_WeaponMenu_EntryStatActive (entry, stats, num_stats);

	if (active && stats == cl.stats && entry->model_path &&
		entry->model_path[0] &&
		(entry->source == VR_WEAPON_CATALOG_SOURCE_STOCK ||
		 entry->game_profile ||
		 entry->source == VR_WEAPON_CATALOG_SOURCE_DISCOVERED ||
		 entry->active_stat == STAT_ACTIVEWEAPON))
	{
		const char *observed_path = VR_WeaponMenu_CurrentModelPath (stats,
			num_stats);
		if (observed_path && !VR_WeaponMenu_EntryModelMatches (entry, observed_path, stats, num_stats))
			return false;
	}
	return active;
}

static qboolean VR_WeaponMenu_EntryOwned (const vr_weapon_menu_entry_t *entry,
	const int *stats, size_t num_stats, int client_items, qboolean active)
{
	int stat;
	qboolean owned = false;
	if (entry->source == VR_WEAPON_CATALOG_SOURCE_DISCOVERED)
		return active;
	if (entry->owned_stat >= 0)
	{
		stat = entry->owned_stat == STAT_ITEMS ?
			(VR_WeaponMenu_Stat (stats, num_stats, STAT_ITEMS) | client_items) :
			VR_WeaponMenu_Stat (stats, num_stats, entry->owned_stat);
		owned = entry->owned_mask ? (stat & entry->owned_mask) != 0 : stat != 0;
	}
	else if (entry->selector && !(entry->schema_fields & VR_WHEEL_OWNERSHIP_FIELDS))
	{
		if (entry->game_profile)
			owned = (VR_WeaponMenu_Stat (stats, num_stats, STAT_VR_WEAPONS) &
				entry->selector) != 0;
		if (!entry->game_profile ||
			VR_WeaponMenu_ProfileUsesItemOwnership (entry->selector))
			owned = owned || ((client_items |
				VR_WeaponMenu_Stat (stats, num_stats, STAT_ITEMS)) &
				entry->selector) != 0;
	}
	return owned || active;
}

static qboolean VR_WeaponMenu_EntrySelectable (const vr_weapon_menu_entry_t *entry,
	qboolean active, const int *stats, size_t num_stats)
{
	int ammo;
	if (entry->kind != VR_WEAPON_MENU_WEAPON ||
		entry->source == VR_WEAPON_CATALOG_SOURCE_DISCOVERED ||
		entry->impulse <= 0)
		return false;
	if (active || entry->ammo_stat < 0 || entry->ammo_stat == STAT_AMMO)
		return true;
	if ((size_t)entry->ammo_stat >= num_stats)
		return true;
	ammo = VR_WeaponMenu_Stat (stats, num_stats, entry->ammo_stat);
	return ammo > 0;
}

static qboolean VR_WeaponMenu_CatalogEntryOwned (const vr_weapon_menu_catalog_t *catalog,
	size_t i, const int *stats, size_t num_stats, int client_items, qboolean active)
{
	const vr_weapon_menu_entry_t *entry = &catalog->entries[i];
	qboolean owned = VR_WeaponMenu_EntryOwned (entry, stats, num_stats, client_items, active);
	if (catalog == &vr_weapon_menu_wwheel_catalog &&
		i < VR_WEAPON_MENU_MAX_ENTRIES && vr_weapon_menu_schema_bitmasks[i] &&
		!(entry->schema_fields & VR_WHEEL_OWNERSHIP_FIELDS))
	{
		const int mask = vr_weapon_menu_schema_bitmasks[i];
		int ownership = VR_WeaponMenu_Stat (stats, num_stats, STAT_VR_WEAPONS);
		int stock_item_bits = 0;
		for (size_t stock = 0; stock < sizeof (vr_weapon_menu_stock_entries) /
			sizeof (vr_weapon_menu_stock_entries[0]); ++stock)
			stock_item_bits |= vr_weapon_menu_stock_entries[stock].selector;
		if ((mask & stock_item_bits) == mask)
			ownership |= client_items |
				VR_WeaponMenu_Stat (stats, num_stats, STAT_ITEMS);
		owned = owned || (ownership & mask) != 0;
	}
	return owned;
}

/* NULL means visible; the wheel and diagnostic share this live identity policy. */
static const char *VR_WeaponMenu_Visibility (const vr_weapon_menu_catalog_t *catalog,
	size_t index, const int *stats, size_t num_stats, int client_items,
	qboolean owned, qboolean active)
{
	const vr_weapon_menu_entry_t *entry = &catalog->entries[index];
	qboolean schema_peer = false, profile_peer = false, stock_peer = false;
	for (size_t i = 0; i < catalog->count; ++i)
	{
		const vr_weapon_menu_entry_t *other = &catalog->entries[i];
		if (i == index || !VR_WeaponCatalog_IdentitiesCompatible (
			VR_WeaponMenu_EntryIdentity (entry), VR_WeaponMenu_EntryIdentity (other)))
			continue;
		schema_peer |= other->source == VR_WEAPON_CATALOG_SOURCE_SCHEMA;
		profile_peer |= other->game_profile;
		if (!catalog->authoritative_schema && other->source == VR_WEAPON_CATALOG_SOURCE_STOCK)
			stock_peer |= VR_WeaponMenu_EntryOwned (other, stats, num_stats, client_items,
				VR_WeaponMenu_EntryActive (other, stats, num_stats));
	}
	if (entry->source == VR_WEAPON_CATALOG_SOURCE_DISCOVERED && stock_peer)
		return "known-slot-observation";
	if (VR_WeaponCatalog_ShouldExpose (entry->source, catalog->authoritative_schema,
		schema_peer, profile_peer, owned, active))
		return NULL;
	if (!owned && !active)
		return "unowned";
	if (entry->source != VR_WEAPON_CATALOG_SOURCE_SCHEMA && schema_peer)
		return "schema-fallback";
	if (entry->source != VR_WEAPON_CATALOG_SOURCE_SCHEMA &&
		entry->source != VR_WEAPON_CATALOG_SOURCE_PROFILE && profile_peer)
		return "profile-fallback";
	return "complete-roster";
}

void VR_WeaponMenu_List_f (void)
{
	const vr_weapon_menu_catalog_t *catalog = VR_WeaponMenu_CurrentCatalog ();
	const char *active_model = VR_WeaponMenu_CurrentModelPath (cl.stats, MAX_CL_STATS);
	int visible = 0;
	Con_Printf ("VR weapon list: game=%s entries=%d complete=%d\n",
		COM_SkipPath (com_gamedir), (int)catalog->count, catalog->authoritative_schema);
	Con_Printf ("stats: items=%d cl.items=%d active=%d model=%s\n",
		cl.stats[STAT_ITEMS], cl.items, cl.stats[STAT_ACTIVEWEAPON], active_model ? active_model : "none");
	for (size_t i = 0; i < catalog->count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = &catalog->entries[i];
		const qboolean active = VR_WeaponMenu_EntryActive (entry, cl.stats, MAX_CL_STATS);
		const qboolean owned = VR_WeaponMenu_CatalogEntryOwned (catalog, i, cl.stats, MAX_CL_STATS, cl.items, active);
		const char *reason = VR_WeaponMenu_Visibility (catalog, i, cl.stats, MAX_CL_STATS, cl.items, owned, active);
		const char *source = entry->source == VR_WEAPON_CATALOG_SOURCE_SCHEMA ? "schema" :
			entry->source == VR_WEAPON_CATALOG_SOURCE_PROFILE ? "profile" :
			entry->source == VR_WEAPON_CATALOG_SOURCE_DISCOVERED ? "observed" : "stock";
		const char *preview = VR_WeaponMenu_EntryPreviewPath (entry, cl.stats, MAX_CL_STATS);
		const char *held = VR_WeaponMenu_EntryViewmodel (entry, cl.stats, MAX_CL_STATS);
		visible += reason == NULL;
		Con_Printf ("[%03d] %s source=%s profile=%d selector=%d impulse=%d owned=%d active=%d "
			"owned_stat=%d value=%d mask=%d active_stat=%d value=%d mask=%d preview=%s held=%s\n",
			(int)i, reason ? reason : "visible", source, entry->game_profile, entry->selector, entry->impulse,
			owned, active, entry->owned_stat,
			entry->owned_stat == STAT_ITEMS ? cl.items | cl.stats[STAT_ITEMS] :
				VR_WeaponMenu_Stat (cl.stats, MAX_CL_STATS, entry->owned_stat), entry->owned_mask,
			entry->active_stat, VR_WeaponMenu_Stat (cl.stats, MAX_CL_STATS, entry->active_stat), entry->active_mask,
			preview ? preview : "none", held ? held : "none");
	}
	Con_Printf ("VR weapon list: %d visible, %d hidden\n", visible, (int)catalog->count - visible);
}

static int VR_WeaponMenu_BuildVisible (const vr_weapon_menu_catalog_t *catalog,
	const int *stats, size_t num_stats, int client_items,
	vr_weapon_menu_visible_t *visible, int max_visible)
{
	int count = 0;
	if (!catalog || !catalog->entries || !visible || max_visible <= 0)
		return 0;

	for (size_t i = 0; i < catalog->count && count < max_visible; ++i)
	{
		const vr_weapon_menu_entry_t *entry = &catalog->entries[i];
		const qboolean active = VR_WeaponMenu_EntryActive (entry, stats, num_stats);
		const qboolean owned = VR_WeaponMenu_CatalogEntryOwned (catalog, i, stats, num_stats, client_items, active);
		if (VR_WeaponMenu_Visibility (catalog, i, stats, num_stats, client_items, owned, active))
			continue;

		visible[count].entry = entry;
		visible[count].active = active;
		visible[count].selectable = VR_WeaponMenu_EntrySelectable (entry, active, stats, num_stats);
		/* QuakeC currentammo belongs only to the equipped weapon. */
		visible[count].ammo = entry->ammo_stat >= 0 &&
			(entry->ammo_stat != STAT_AMMO || active) ?
			VR_WeaponMenu_Stat (stats, num_stats, entry->ammo_stat) : -1;
		visible[count].ammo_max = VR_WeaponCatalog_ResolveAmmoMax (entry->ammo_max,
			VR_WeaponMenu_Stat (stats, num_stats, entry->ammo_max_stat));
		count++;
	}
	return count;
}

static void VR_WeaponMenu_Pointer (int *x, int *y)
{
	int pixel_x = 0, pixel_y = 0;
	IN_GetMousePos (&pixel_x, &pixel_y);
	*x = vid.width > 0 ? (int)((double)pixel_x * glwidth / vid.width) : -1;
	*y = vid.height > 0 ? (int)((double)pixel_y * glheight / vid.height) : -1;
}

static int VR_WeaponMenu_Hit (vr_weapon_menu_visible_t *visible, int count,
	int pointer_x, int pointer_y, float outer_radius)
{
	const float center_x = glwidth * 0.5f;
	const float center_y = glheight * 0.5f;
	int best = -1;
	float best_distance = FLT_MAX;

	/* The visible label/icon boxes are tested first; these are exactly the
	 * rectangles drawn below, including on high-DPI and resized windows. */
	for (int i = 0; i < count; ++i)
	{
		float dx, dy, distance;
		if (!visible[i].selectable || pointer_x < visible[i].left ||
			pointer_x > visible[i].left + visible[i].width ||
			pointer_y < visible[i].top || pointer_y > visible[i].top + visible[i].height)
			continue;
		dx = pointer_x - visible[i].center_x;
		dy = pointer_y - visible[i].center_y;
		distance = dx * dx + dy * dy;
		if (distance < best_distance)
		{
			best_distance = distance;
			best = i;
		}
	}
	if (best >= 0)
		return best;

	/* The annular hit region follows the radial slots. Its angular subdivision
	 * uses the same slot centers as the labels rather than screen-space guesses. */
	if (count > 0)
	{
		const float dx = pointer_x - center_x;
		const float dy = pointer_y - center_y;
		const float radius = sqrtf (dx * dx + dy * dy);
		if (radius >= outer_radius * 0.38f && radius <= outer_radius &&
			count <= 16)
		{
			float angle = atan2f (dy, dx) + 1.57079632679f;
			int slot;
			const float full_turn = 6.28318530718f;
			if (angle < 0.0f)
				angle += full_turn;
			if (angle >= full_turn)
				angle -= full_turn;
			slot = (int)(angle * count / full_turn + 0.5f) % count;
			if (visible[slot].selectable)
				return slot;
		}
	}
	return -1;
}

enum {
	VR_WEAPON_MENU_QUICK_SAVE,
	VR_WEAPON_MENU_QUICK_LOAD,
	VR_WEAPON_MENU_ACTION_COUNT
};

#define VR_WEAPON_MENU_MAX_ACTIONS \
	(VR_WEAPON_MENU_ACTION_COUNT + MAX_SCOREBOARD + 1)

typedef enum {
	VR_WEAPON_MENU_ACTION_QUICK_SAVE,
	VR_WEAPON_MENU_ACTION_QUICK_LOAD,
	VR_WEAPON_MENU_ACTION_COOP_PLAYER,
	VR_WEAPON_MENU_ACTION_COOP_SPAWN
} vr_weapon_menu_action_kind_t;

typedef struct {
	vr_weapon_menu_action_kind_t kind;
	int id;
	int slot;
	char label[MAX_SCOREBOARDNAME];
	char player_name[MAX_SCOREBOARDNAME];
	float color[2][3]; /* prepared normal/hover colors for scene and UI tasks */
	float left;
	float top;
	float width;
	float height;
} vr_weapon_menu_action_t;

typedef struct {
	unsigned int generation;
	int count;
	int action_count;
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	vr_weapon_menu_entry_t entries[VR_WEAPON_MENU_MAX_ENTRIES];
	qmodel_t *model[VR_WEAPON_MENU_MAX_ENTRIES];
	aliashdr_t *geometry[VR_WEAPON_MENU_MAX_ENTRIES];
	float world_from_ndc[16];
	float model_yaw;
	float outer_radius;
	qboolean panel_valid;
	qboolean playspace;
	char labels[VR_WEAPON_MENU_MAX_ENTRIES][MAX_QPATH];
	char models[VR_WEAPON_MENU_MAX_ENTRIES][MAX_QPATH];
	vr_weapon_menu_action_t actions[VR_WEAPON_MENU_MAX_ACTIONS];
} vr_weapon_menu_frame_t;

/* Prepared during setup, then read by both the scene and GUI tasks. Release
 * deliberately rechecks live catalog and inventory instead of trusting it. */
static vr_weapon_menu_frame_t vr_weapon_menu_frame;

/* Indexed by the active catalog, then copied into the draw frame. Model loading
 * is only permitted on the SCR_UpdateScreen main-thread side of task dispatch. */
static struct {
	unsigned int generation;
	const vr_weapon_menu_catalog_t *catalog;
	qmodel_t *model[VR_WEAPON_MENU_MAX_ENTRIES];
	aliashdr_t *geometry[VR_WEAPON_MENU_MAX_ENTRIES];
	qboolean missing[VR_WEAPON_MENU_MAX_ENTRIES];
	char preview_paths[VR_WEAPON_MENU_MAX_ENTRIES][MAX_QPATH];
	char held_paths[VR_WEAPON_MENU_MAX_ENTRIES][MAX_QPATH];
} vr_weapon_menu_assets;

static void VR_WeaponMenu_InvalidateFrameModels (void)
{
	vr_weapon_menu_frame_valid = false;
	vr_weapon_menu_frame.panel_valid = false;
	for (int i = 0; i < VR_WEAPON_MENU_MAX_ENTRIES; ++i)
	{
		vr_weapon_menu_frame.model[i] = NULL;
		vr_weapon_menu_frame.geometry[i] = NULL;
	}
}

static void VR_WeaponMenu_InvalidateModelSlot (
	const vr_weapon_menu_catalog_t *catalog, size_t index)
{
	if (vr_weapon_menu_assets.catalog == catalog &&
		index < VR_WEAPON_MENU_MAX_ENTRIES)
	{
		vr_weapon_menu_assets.model[index] = NULL;
		vr_weapon_menu_assets.geometry[index] = NULL;
		vr_weapon_menu_assets.missing[index] = false;
	}
	VR_WeaponMenu_InvalidateFrameModels ();
}

void VR_WeaponMenu_ClientReset (void)
{
	VR_WeaponMenu_ClearSession ();
	if (++vr_weapon_menu_session_generation == 0)
		++vr_weapon_menu_session_generation;
	memset (&vr_weapon_menu_frame, 0, sizeof (vr_weapon_menu_frame));
	memset (&vr_weapon_menu_assets, 0, sizeof (vr_weapon_menu_assets));
}

static qboolean VR_WeaponMenu_EnsureMutableCatalogForDiscovery (void)
{
	if (VR_WeaponMenu_CurrentCatalog () != &vr_weapon_menu_stock_catalog)
		return true;
	if (vr_weapon_menu_stock_catalog.count > VR_WEAPON_MENU_MAX_ENTRIES)
		return false;
	VR_WeaponMenu_SeedStock ();
	vr_weapon_menu_has_discoveries = true;
	return true;
}

static void VR_WeaponMenu_LearnSchemaModel (size_t index,
	const char *model_path)
{
	vr_weapon_menu_entry_t *entry = &vr_weapon_menu_wwheel_entries[index];
	if (!model_path || !model_path[0] || entry->game_profile ||
		(entry->schema_fields & VR_SCHEMA_WHEEL_VIEWMODEL))
		return;
	/* Observe identity without replacing authored display geometry. Both paths
	 * have catalog lifetime, independent of the current map precache. */
	q_strlcpy (vr_weapon_menu_held_models[index], model_path,
		sizeof (vr_weapon_menu_held_models[index]));
	entry->viewmodel_path = vr_weapon_menu_held_models[index];
	if (!(entry->schema_fields & VR_SCHEMA_WHEEL_MODEL))
	{
		q_strlcpy (vr_weapon_menu_runtime_models[index], model_path,
			sizeof (vr_weapon_menu_runtime_models[index]));
		entry->model_path = vr_weapon_menu_runtime_models[index];
	}
	VR_WeaponMenu_InvalidateModelSlot (&vr_weapon_menu_wwheel_catalog, index);
}

static vr_wheel_match_t VR_WeaponMenu_FindDiscoveredModel (
	const vr_weapon_menu_catalog_t *catalog, int selector,
	const char *model_path, size_t *index)
{
	int best_rank = -1;
	vr_wheel_match_t result = VR_WHEEL_NOT_FOUND;
	if (!catalog || !catalog->entries || !model_path)
		return result;
	for (size_t i = 0; i < catalog->count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = &catalog->entries[i];
		if (entry->kind != VR_WEAPON_MENU_WEAPON ||
			!(entry->active_stat >= 0 ? VR_WeaponMenu_EntryStatActive (entry, cl.stats, MAX_CL_STATS) :
			  VR_WeaponMenu_EntryMatchesSelector (entry, selector)) ||
			!VR_WeaponMenu_EntryModelMatches (entry, model_path, cl.stats, MAX_CL_STATS))
			continue;
		const int rank = entry->viewmodel_path &&
			!q_strcasecmp (entry->viewmodel_path, model_path) ? 2 :
			entry->model_path && !q_strcasecmp (entry->model_path, model_path) ? 1 : 0;
		if (rank > best_rank)
		{
			best_rank = rank;
			*index = i;
			result = VR_WHEEL_MATCH;
		}
		else if (rank == best_rank)
			result = VR_WHEEL_AMBIGUOUS;
	}
	return result;
}

static vr_wheel_match_t VR_WeaponMenu_FindLearnableSchemaSlot (
	const vr_weapon_menu_catalog_t *catalog, int selector, size_t *index)
{
	vr_wheel_match_t result = VR_WHEEL_NOT_FOUND;
	if (!catalog || catalog != &vr_weapon_menu_wwheel_catalog)
		return result;
	for (size_t i = 0; i < catalog->count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = &catalog->entries[i];
		if (entry->kind != VR_WEAPON_MENU_WEAPON ||
			entry->source != VR_WEAPON_CATALOG_SOURCE_SCHEMA || entry->impulse <= 0 ||
			entry->game_profile || (entry->schema_fields & VR_SCHEMA_WHEEL_VIEWMODEL) ||
			vr_weapon_menu_held_models[i][0] ||
			!(entry->active_stat >= 0 ? VR_WeaponMenu_EntryStatActive (entry, cl.stats, MAX_CL_STATS) :
			  VR_WeaponMenu_EntryMatchesSelector (entry, selector)))
			continue;
		if (result != VR_WHEEL_NOT_FOUND)
			return VR_WHEEL_AMBIGUOUS;
		*index = i;
		result = VR_WHEEL_MATCH;
	}
	return result;
}

static int VR_WeaponMenu_AddDiscoveredEntry (int selector,
	const char *model_path)
{
	const vr_weapon_menu_catalog_t *catalog;
	vr_weapon_menu_entry_t *entry;
	char model_label[32];
	size_t index;
	int id;

	if (!VR_WeaponMenu_EnsureMutableCatalogForDiscovery ())
		return -1;
	catalog = &vr_weapon_menu_wwheel_catalog;
	if (catalog->count >= VR_WEAPON_MENU_MAX_ENTRIES)
		return -1;
	id = VR_WeaponMenu_AllocateStableID (catalog,
		0x40000000 + (selector & 0x1fffffff));
	if (id < 0)
		return -1;
	index = catalog->count;
	entry = &vr_weapon_menu_wwheel_entries[index];
	memset (entry, 0, sizeof (*entry));
	q_strlcpy (vr_weapon_menu_runtime_models[index], model_path,
		sizeof (vr_weapon_menu_runtime_models[index]));
	VR_WeaponMenu_SchemaLabel (model_label, sizeof (model_label), model_path,
		selector);
	q_snprintf (vr_weapon_menu_wwheel_labels[index],
		sizeof (vr_weapon_menu_wwheel_labels[index]), "DISCOVERED %.20s",
		model_label);
	entry->id = id;
	entry->kind = VR_WEAPON_MENU_WEAPON;
	entry->source = VR_WEAPON_CATALOG_SOURCE_DISCOVERED;
	entry->label = vr_weapon_menu_wwheel_labels[index];
	entry->model_path = vr_weapon_menu_runtime_models[index];
	entry->model_scale = 1.0f;
	entry->selector = selector;
	entry->impulse = 0;
	entry->owned_stat = -1;
	entry->owned_mask = 0;
	/* The fallback EntryActive path compares STAT_ACTIVEWEAPON exactly;
	 * discovered selectors must never inherit bit-mask semantics. */
	entry->active_stat = -1;
	entry->active_mask = 0;
	entry->ammo_stat = -1;
	entry->ammo_max_stat = -1;
	vr_weapon_menu_wwheel_catalog.count++;
	vr_weapon_menu_has_discoveries = true;
	VR_WeaponMenu_InvalidateModelSlot (catalog, index);
	return (int)index;
}

void VR_WeaponMenu_ObserveActive (void)
{
	const int selector = cl.stats[STAT_ACTIVEWEAPON];
	const char *model_path;
	const vr_weapon_menu_catalog_t *catalog;
	size_t index = 0;

	if (cls.state != ca_connected || cls.signon != SIGNONS || selector == 0)
		return;
	model_path = VR_WeaponMenu_CurrentModelPath (cl.stats, MAX_CL_STATS);
	if (!VR_WeaponMenu_ModelPathLooksWeapon (model_path))
		return;
	catalog = VR_WeaponMenu_CurrentCatalog ();
	vr_wheel_match_t match = VR_WeaponMenu_FindDiscoveredModel (catalog, selector, model_path, &index);
	if (match == VR_WHEEL_MATCH)
		return;
	if (match != VR_WHEEL_AMBIGUOUS)
	{
		match = VR_WeaponMenu_FindLearnableSchemaSlot (catalog, selector, &index);
		if (match == VR_WHEEL_MATCH)
		{
			VR_WeaponMenu_LearnSchemaModel (index, model_path);
			return;
		}
	}
	if (match == VR_WHEEL_AMBIGUOUS)
	{
		/* Do not turn ambiguity into a learned identity or another row. */
		Con_DPrintf ("VR: ambiguous observed weapon selector %d; declare held identity\n", selector);
		return;
	}
	VR_WeaponMenu_AddDiscoveredEntry (selector, model_path);
}

void VR_WeaponMenu_PrepareModels (void)
{
	const vr_weapon_menu_catalog_t *catalog;
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	int count;

	if (!VR_WeaponMenu_IsOpenVR () || !VR_WeaponMenu_SessionValid ())
		return;
	catalog = VR_WeaponMenu_CurrentCatalog ();
	if (vr_weapon_menu_assets.generation != vr_weapon_menu_session_generation ||
		vr_weapon_menu_assets.catalog != catalog)
	{
		memset (&vr_weapon_menu_assets, 0, sizeof (vr_weapon_menu_assets));
		vr_weapon_menu_assets.generation = vr_weapon_menu_session_generation;
		vr_weapon_menu_assets.catalog = catalog;
	}
	count = VR_WeaponMenu_BuildVisible (catalog, cl.stats, MAX_CL_STATS,
		cl.items, visible, VR_WEAPON_MENU_MAX_ENTRIES);
	for (int i = 0; i < count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = visible[i].entry;
		const size_t index = (size_t)(entry - catalog->entries);
		const char *path = VR_WeaponMenu_EntryPreviewPath (entry, cl.stats, MAX_CL_STATS);
		const char *held = VR_WeaponMenu_EntryViewmodel (entry, cl.stats, MAX_CL_STATS);
		qmodel_t *model;
		if (index >= VR_WEAPON_MENU_MAX_ENTRIES)
			continue;
		/* An upgrade or override can change paths without a session reset.
		 * Invalidate before consulting missing, including exact-held fallback. */
		if (q_strcasecmp (vr_weapon_menu_assets.preview_paths[index], path ? path : "") ||
			q_strcasecmp (vr_weapon_menu_assets.held_paths[index], held ? held : ""))
		{
			VR_WeaponMenu_InvalidateModelSlot (catalog, index);
			q_strlcpy (vr_weapon_menu_assets.preview_paths[index], path ? path : "",
				sizeof (vr_weapon_menu_assets.preview_paths[index]));
			q_strlcpy (vr_weapon_menu_assets.held_paths[index], held ? held : "",
				sizeof (vr_weapon_menu_assets.held_paths[index]));
		}
		if (!path || !*path || vr_weapon_menu_assets.missing[index])
			continue;
		model = vr_weapon_menu_assets.model[index];
		if (!model)
			model = Mod_ForName (path, false);
		if (!model || model->type != mod_alias)
		{
			if (held && held[0])
				model = Mod_ForName (held, false);
		}
		if ((!model || model->type != mod_alias) &&
			(!held || !held[0]))
		{
			/* Use a provisional basename only without known held identity;
			 * a missing known model must not select a different weapon. */
			char viewmodel[MAX_QPATH];
			char *pickup;
			q_strlcpy (viewmodel, path, sizeof (viewmodel));
			pickup = strstr (viewmodel, "/g_");
			if (pickup)
			{
				pickup[1] = 'v';
				model = Mod_ForName (viewmodel, false);
			}
		}
		if (!model || model->type != mod_alias)
		{
			vr_weapon_menu_assets.missing[index] = true;
			continue;
		}
		vr_weapon_menu_assets.model[index] = model;
		vr_weapon_menu_assets.geometry[index] =
			(aliashdr_t *)Mod_Extradata_CheckSkin (model, 0);
	}
}

static int VR_WeaponMenu_RingCount (int visible_count)
{
	int rings = 1;
	int capacity = 1;

	if (visible_count <= 0)
		return 0;
	while (capacity < visible_count && rings <= VR_WEAPON_MENU_MAX_ENTRIES)
	{
		capacity += 8 * rings;
		++rings;
	}
	return rings;
}

int VR_WeaponMenu_VisibleRingCount (void)
{
	const vr_weapon_menu_catalog_t *catalog;
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	int visible_count;

	if (!VR_WeaponMenu_IsOpenVR () || !VR_WeaponMenu_SessionValid ())
		return 0;
	catalog = VR_WeaponMenu_CurrentCatalog ();
	visible_count = VR_WeaponMenu_BuildVisible (catalog, cl.stats,
		MAX_CL_STATS, cl.items, visible, VR_WEAPON_MENU_MAX_ENTRIES);
	return VR_WeaponMenu_RingCount (visible_count);
}

static qboolean VR_WeaponMenu_QuickSaveAvailable (void)
{
	return sv.active && svs.maxclients == 1 && !cls.demoplayback &&
		!cl.intermission;
}

static qboolean VR_WeaponMenu_CoopPlayersAvailable (void)
{
	return isfinite (vr_weaponmenu_player_teleport.value) &&
		vr_weaponmenu_player_teleport.value != 0.0f &&
		cls.state == ca_connected && cls.signon == SIGNONS &&
		cl.gametype == GAME_COOP && cl.maxclients > 1 && cl.scores != NULL;
}

static qboolean VR_WeaponMenu_CoopSpawnAvailable (void)
{
	return VR_WeaponMenu_CoopPlayersAvailable () && !cl.intermission &&
		cl.stats[STAT_HEALTH] > 0;
}

static qboolean VR_WeaponMenu_CatalogHasActionID (
	const vr_weapon_menu_catalog_t *catalog, int id)
{
	return VR_WeaponMenu_CatalogHasID (catalog, id);
}

static void VR_WeaponMenu_ActionIDs (const vr_weapon_menu_catalog_t *catalog,
	int ids[VR_WEAPON_MENU_ACTION_COUNT],
	int stable_ids[MAX_SCOREBOARD + 1])
{
	long long candidate = INT_MAX;

	for (int action = 0; action < VR_WEAPON_MENU_ACTION_COUNT; ++action)
	{
		qboolean used;
		while (candidate > 0)
		{
			used = VR_WeaponMenu_CatalogHasActionID (catalog, (int)candidate);
			for (int i = 0; !used && i < action; ++i)
				used = ids[i] == candidate;
			if (!used)
				break;
			--candidate;
		}
		ids[action] = candidate > 0 ? (int)candidate : -1;
		--candidate;
	}

	/* Negative identities are reserved for dynamic actions. Player IDs start
	 * from their scoreboard slot, so row ordering and missing players do not
	 * change the action identity. Probe around any catalog IDs defensively. */
	for (int identity = 0; identity <= MAX_SCOREBOARD; ++identity)
	{
		long long stable = (long long)INT_MIN + 1 + identity;
		qboolean used;
		do
		{
			used = stable >= 0 || stable == -1 ||
				VR_WeaponMenu_CatalogHasActionID (catalog, (int)stable);
			for (int i = 0; !used && i < VR_WEAPON_MENU_ACTION_COUNT; ++i)
				used = ids[i] == stable;
			for (int i = 0; !used && i < identity; ++i)
				used = stable_ids[i] == stable;
			if (used)
				++stable;
		} while (used && stable <= INT_MAX);
		stable_ids[identity] = stable <= INT_MAX ? (int)stable : -1;
	}
}

static void VR_WeaponMenu_FitActionLabel (char *label, size_t label_size,
	const char *source, float max_width, float scale)
{
	int max_chars;
	size_t source_len = strlen (source);
	size_t copy_len;

	if (!label_size)
		return;
	max_chars = (int)((max_width - 12.0f * scale) /
		(CHARACTER_SIZE * scale));
	if (max_chars < 0)
		max_chars = 0;
	if (source_len <= (size_t)max_chars)
	{
		q_strlcpy (label, source, label_size);
		return;
	}
	if (max_chars >= 3 && label_size >= 4)
	{
		copy_len = (size_t)max_chars - 3;
		if (copy_len > label_size - 4)
			copy_len = label_size - 4;
		memcpy (label, source, copy_len);
		memcpy (label + copy_len, "...", 3);
		label[copy_len + 3] = '\0';
	}
	else
	{
		copy_len = (size_t)max_chars;
		if (copy_len >= label_size)
			copy_len = label_size - 1;
		memcpy (label, source, copy_len);
		label[copy_len] = '\0';
	}
}

static void VR_WeaponMenu_PlayerLabelColor (int slot, qboolean is_selected,
	float color[3])
{
	const float min_peak = is_selected ? 0.85f : 0.38f;
	int topcolor;
	const byte *rgb;
	float maxc, boost;

	if (!cl.scores || slot < 0 || slot >= cl.maxclients ||
		slot >= MAX_SCOREBOARD)
	{
		color[0] = color[1] = color[2] = 0.82f;
		return;
	}

	topcolor = (cl.scores[slot].colors >> 4) & 0xF;
	rgb = (const byte *)&d_8to24table[topcolor * 16 + 8];
	color[0] = rgb[0] / 255.0f;
	color[1] = rgb[1] / 255.0f;
	color[2] = rgb[2] / 255.0f;
	maxc = q_max (color[0], q_max (color[1], color[2]));
	if (maxc <= 0.0f)
	{
		color[0] = color[1] = color[2] = min_peak;
		return;
	}

	boost = is_selected ? 2.4f : 0.9f;
	if (maxc * boost < min_peak)
		boost = min_peak / maxc;
	color[0] *= boost;
	color[1] *= boost;
	color[2] *= boost;
}

static void VR_WeaponMenu_DrawOutlinedActionText (cb_context_t *context,
	float x, float y, const char *text, float scale, const float color[3])
{
	GL_SetCanvasColor (0.0f, 0.0f, 0.0f, 1.0f);
	Draw_String_Scaled (context, x - scale, y, text, scale);
	Draw_String_Scaled (context, x + scale, y, text, scale);
	Draw_String_Scaled (context, x, y - scale, text, scale);
	Draw_String_Scaled (context, x, y + scale, text, scale);
	/* The donor uses overbright floats here. vkQuake packs canvas colors into
	 * bytes, so values above one must saturate instead of wrapping to dark. */
	GL_SetCanvasColor (q_min (color[0], 1.0f), q_min (color[1], 1.0f),
		q_min (color[2], 1.0f), 1.0f);
	Draw_String_Scaled (context, x, y, text, scale);
	GL_SetCanvasColor (1.0f, 1.0f, 1.0f, 1.0f);
}

static int VR_WeaponMenu_BuildActions (const vr_weapon_menu_catalog_t *catalog,
	qboolean quick_available, float outer_radius, float scale,
	vr_weapon_menu_action_t actions[VR_WEAPON_MENU_MAX_ACTIONS])
{
	static const char *const quick_labels[VR_WEAPON_MENU_ACTION_COUNT] = {
		"QUICK SAVE", "QUICK LOAD"
	};
	int quick_ids[VR_WEAPON_MENU_ACTION_COUNT];
	int stable_ids[MAX_SCOREBOARD + 1];
	const float center_x = glwidth * 0.5f;
	const float center_y = glheight * 0.5f;
	const float right_left = center_x + outer_radius + 12.0f * scale;
	const float max_width = q_max (0.0f, glwidth - right_left - 4.0f * scale);
	int count = 0;
	int teleport_start;
	int teleport_count;

	VR_WeaponMenu_ActionIDs (catalog, quick_ids, stable_ids);
	if (quick_available)
		for (int i = 0; i < VR_WEAPON_MENU_ACTION_COUNT; ++i)
		{
			vr_weapon_menu_action_t *action = &actions[count++];
			memset (action, 0, sizeof (*action));
			action->kind = i == VR_WEAPON_MENU_QUICK_SAVE ?
				VR_WEAPON_MENU_ACTION_QUICK_SAVE : VR_WEAPON_MENU_ACTION_QUICK_LOAD;
			action->id = quick_ids[i];
			action->slot = -1;
			q_strlcpy (action->label, quick_labels[i], sizeof (action->label));
			action->width = strlen (action->label) * CHARACTER_SIZE * scale +
				12.0f * scale;
			action->height = 26.0f * scale;
			action->left = center_x - outer_radius - 12.0f * scale - action->width;
			action->top = center_y + (i == VR_WEAPON_MENU_QUICK_SAVE ?
				-17.0f : 17.0f) * scale - action->height * 0.5f;
		}

	teleport_start = count;
	if (VR_WeaponMenu_CoopPlayersAvailable ())
		for (int slot = 0; slot < cl.maxclients && slot < MAX_SCOREBOARD; ++slot)
		{
			vr_weapon_menu_action_t *action;
			if (!cl.scores[slot].name[0] || slot + 1 == cl.viewentity)
				continue;
			action = &actions[count++];
			memset (action, 0, sizeof (*action));
			action->kind = VR_WEAPON_MENU_ACTION_COOP_PLAYER;
			action->id = stable_ids[slot];
			action->slot = slot;
			q_strlcpy (action->player_name, cl.scores[slot].name,
				sizeof (action->player_name));
		}
	if (VR_WeaponMenu_CoopSpawnAvailable ())
	{
		vr_weapon_menu_action_t *action = &actions[count++];
		memset (action, 0, sizeof (*action));
		action->kind = VR_WEAPON_MENU_ACTION_COOP_SPAWN;
		action->id = stable_ids[MAX_SCOREBOARD];
		action->slot = -1;
		q_strlcpy (action->player_name, "RESPAWN AT SPAWN",
			sizeof (action->player_name));
	}

	teleport_count = count - teleport_start;
	for (int i = 0; i < teleport_count; ++i)
	{
		vr_weapon_menu_action_t *action = &actions[teleport_start + i];
		const float row_pitch = q_min (29.0f * scale,
			q_max (0.0f, glheight - 8.0f * scale) / teleport_count);
		const float row_height = q_min (26.0f * scale, row_pitch);
		VR_WeaponMenu_FitActionLabel (action->label, sizeof (action->label),
			action->player_name, max_width, scale);
		action->height = row_height;
		action->width = q_min ((float)strlen (action->label) *
				CHARACTER_SIZE * scale + 12.0f * scale, max_width);
		action->left = right_left;
		action->top = center_y - teleport_count * row_pitch * 0.5f +
			(row_pitch - row_height) * 0.5f + i * row_pitch;
	}
	for (int i = 0; i < count; ++i)
	{
		vr_weapon_menu_action_t *action = &actions[i];
		if (action->kind == VR_WEAPON_MENU_ACTION_COOP_PLAYER)
		{
			VR_WeaponMenu_PlayerLabelColor (action->slot, false, action->color[0]);
			VR_WeaponMenu_PlayerLabelColor (action->slot, true, action->color[1]);
		}
		else
		{
			action->color[0][0] = action->color[0][1] =
				action->color[0][2] = 0.82f;
			action->color[1][0] = action->color[1][2] = 0.45f;
			action->color[1][1] = 1.85f;
		}
	}
	return count;
}

/* One geometry and hit-test path keeps every action rectangle aligned with
 * drawing for both the desktop mouse and the OpenXR pointer. */
static int VR_WeaponMenu_Actions (struct cb_context_s *context,
	vr_weapon_menu_action_t actions[VR_WEAPON_MENU_MAX_ACTIONS], int count,
	float scale, int pointer_x, int pointer_y, qboolean highlight_by_id,
	int hover_id)
{
	int selected = -1;

	for (int i = 0; i < count; ++i)
		if (actions[i].width > 0.0f && actions[i].height > 0.0f &&
			pointer_x >= actions[i].left &&
			pointer_x <= actions[i].left + actions[i].width &&
			pointer_y >= actions[i].top &&
			pointer_y <= actions[i].top + actions[i].height)
			selected = i;
	if (context)
		for (int i = 0; i < count; ++i)
		{
			const qboolean is_selected = i == selected &&
				(!highlight_by_id || actions[i].id == hover_id);

			Draw_Fill ((cb_context_t *)context, actions[i].left, actions[i].top,
				actions[i].width, actions[i].height,
				is_selected ? 15 : 0, is_selected ? 0.96f : 0.78f);
			VR_WeaponMenu_DrawOutlinedActionText ((cb_context_t *)context,
				actions[i].left + 6.0f * scale,
				actions[i].top + (actions[i].kind == VR_WEAPON_MENU_ACTION_COOP_PLAYER ||
				 actions[i].kind == VR_WEAPON_MENU_ACTION_COOP_SPAWN ?
				 q_max (0.0f, (actions[i].height - 8.0f * scale) * 0.5f) :
				 8.0f * scale), actions[i].label, scale,
				actions[i].color[is_selected ? 1 : 0]);
		}
	return selected;
}

static qboolean VR_WeaponMenu_ActionStillValid (
	const vr_weapon_menu_action_t *action)
{
	if (!action)
		return false;
	switch (action->kind)
	{
	case VR_WEAPON_MENU_ACTION_QUICK_SAVE:
	case VR_WEAPON_MENU_ACTION_QUICK_LOAD:
		return VR_WeaponMenu_QuickSaveAvailable ();
	case VR_WEAPON_MENU_ACTION_COOP_PLAYER:
		return VR_WeaponMenu_CoopPlayersAvailable () && action->slot >= 0 &&
			action->slot < cl.maxclients && action->slot < MAX_SCOREBOARD &&
			action->slot + 1 != cl.viewentity &&
			cl.scores[action->slot].name[0] &&
			!strcmp (cl.scores[action->slot].name, action->player_name);
	case VR_WEAPON_MENU_ACTION_COOP_SPAWN:
		return VR_WeaponMenu_CoopSpawnAvailable ();
	default:
		return false;
	}
}

static void VR_WeaponMenu_SetSlotGeometry (vr_weapon_menu_visible_t *visible,
	int index, float center_x, float center_y, float scale)
{
	const float text_width = visible[index].entry->label ?
		strlen (visible[index].entry->label) * CHARACTER_SIZE * scale : 0.0f;
	const float box_width = q_max (text_width + 12.0f * scale, 40.0f * scale);
	visible[index].center_x = center_x;
	visible[index].center_y = center_y;
	visible[index].width = box_width;
	visible[index].height = 44.0f * scale;
	visible[index].left = center_x - box_width * 0.5f;
	visible[index].top = center_y - visible[index].height * 0.5f;
}

static void VR_WeaponMenu_Layout (vr_weapon_menu_visible_t *visible, int count,
	float outer_radius, float scale)
{
	const float center_x = glwidth * 0.5f;
	const float center_y = glheight * 0.5f;
	const float label_radius = outer_radius * 0.80f;
	const float angle_step = count > 0 ? 6.28318530718f / count : 0.0f;

	for (int i = 0; i < count; ++i)
	{
		const float angle = -1.57079632679f + angle_step * i;
		VR_WeaponMenu_SetSlotGeometry (visible, i,
			center_x + cosf (angle) * label_radius,
			center_y + sinf (angle) * label_radius, scale);
	}
}

static float VR_WeaponMenu_LayoutPlayspace (vr_weapon_menu_visible_t *visible,
	int count, float world_units_per_pixel, float scale)
{
	const float center_x = glwidth * 0.5f;
	const float center_y = glheight * 0.5f;
	int index = 0;
	int ring = 0;

	if (!isfinite (world_units_per_pixel) || world_units_per_pixel <= 0.0f)
		return 0.0f;
	if (count <= 0)
		return 5.0f / world_units_per_pixel;
	VR_WeaponMenu_SetSlotGeometry (visible, index++, center_x, center_y, scale);
	if (count == 1)
		return 5.0f / world_units_per_pixel;
	for (ring = 1; index < count; ++ring)
	{
		const int capacity = 8 * ring;
		const int in_ring = q_min (capacity, count - index);
		const float radius = 5.0f * ring / world_units_per_pixel;
		const float angle_step = 6.28318530718f / in_ring;

		for (int slot = 0; slot < in_ring; ++slot)
		{
			const float angle = -1.57079632679f + angle_step * slot;
			VR_WeaponMenu_SetSlotGeometry (visible, index++,
				center_x + cosf (angle) * radius,
				center_y + sinf (angle) * radius, scale);
		}
	}
	return 5.0f * q_max (1, ring - 1) / world_units_per_pixel;
}

qboolean VR_WeaponMenu_CanOpen (void)
{
	if (vr_weapon_menu_open || !VR_WeaponMenu_GameContextValid ())
		return false;
	return vulkan_globals.stereo_active ? VR_WeaponMenu_XRContextValid () : true;
}

qboolean VR_WeaponMenu_IsOpen (void)
{
	return vr_weapon_menu_open;
}

qboolean VR_WeaponMenu_IsOpenVR (void)
{
	return vr_weapon_menu_open && vr_weapon_menu_open_vr;
}

unsigned int VR_WeaponMenu_SessionGeneration (void)
{
	return vr_weapon_menu_session_generation;
}

void VR_WeaponMenu_Open (void)
{
	if (!VR_WeaponMenu_CanOpen ())
		return;
	vr_weapon_menu_open = true;
	vr_weapon_menu_open_vr = vulkan_globals.stereo_active;
	vr_weapon_menu_hover_id = vr_weapon_menu_retained_id = -1;
	vr_weapon_menu_hover_action_slot = -1;
	vr_weapon_menu_hover_action_name[0] = '\0';
	if (++vr_weapon_menu_session_generation == 0)
		++vr_weapon_menu_session_generation;
	vr_weapon_menu_worldmodel = cl.worldmodel;
	vr_weapon_menu_viewentity = cl.viewentity;
	q_strlcpy (vr_weapon_menu_mapname, cl.mapname, sizeof (vr_weapon_menu_mapname));
}

void VR_WeaponMenu_Cancel (void)
{
	VR_WeaponMenu_ClearSession ();
}

static float VR_WeaponMenu_ModelYaw (const float world_from_ndc[16])
{
	return atan2f (world_from_ndc[9], world_from_ndc[8]) *
		(180.0f / (float)M_PI) + 180.0f + (float)cl.time * 30.0f;
}

static void VR_WeaponMenu_PrepareFrame (const vr_weapon_menu_catalog_t *catalog,
	float radius, float scale, const float world_from_ndc[16],
	qboolean playspace)
{
	float world_units_per_pixel = 0.0f;
	float layout_radius = radius;
	if (playspace && world_from_ndc)
	{
		world_units_per_pixel = sqrtf (world_from_ndc[8] * world_from_ndc[8] +
			world_from_ndc[9] * world_from_ndc[9] +
			world_from_ndc[10] * world_from_ndc[10]);
	}
	vr_weapon_menu_frame_valid = false;
	vr_weapon_menu_frame.panel_valid = false;
	vr_weapon_menu_frame.count = VR_WeaponMenu_BuildVisible (catalog,
		cl.stats, MAX_CL_STATS, cl.items, vr_weapon_menu_frame.visible,
		VR_WEAPON_MENU_MAX_ENTRIES);
	if (playspace && isfinite (world_units_per_pixel) && world_units_per_pixel > 0.0f)
		layout_radius = VR_WeaponMenu_LayoutPlayspace (vr_weapon_menu_frame.visible,
			vr_weapon_menu_frame.count, world_units_per_pixel, scale);
	else
		VR_WeaponMenu_Layout (vr_weapon_menu_frame.visible,
			vr_weapon_menu_frame.count, radius, scale);
	for (int i = 0; i < vr_weapon_menu_frame.count; ++i)
	{
		vr_weapon_menu_entry_t *copy = &vr_weapon_menu_frame.entries[i];
		const vr_weapon_menu_entry_t *source = vr_weapon_menu_frame.visible[i].entry;
		const size_t index = (size_t)(source - catalog->entries);
		vr_weapon_menu_frame.model[i] = index < catalog->count &&
			index < VR_WEAPON_MENU_MAX_ENTRIES ? vr_weapon_menu_assets.model[index] : NULL;
		vr_weapon_menu_frame.geometry[i] = index < catalog->count &&
			index < VR_WEAPON_MENU_MAX_ENTRIES ? vr_weapon_menu_assets.geometry[index] : NULL;
		*copy = *source;
		copy->model_path = VR_WeaponMenu_EntryPreviewPath (source, cl.stats, MAX_CL_STATS);
		if (copy->label)
		{
			q_strlcpy (vr_weapon_menu_frame.labels[i], copy->label,
				sizeof (vr_weapon_menu_frame.labels[i]));
			copy->label = vr_weapon_menu_frame.labels[i];
		}
		if (copy->model_path)
		{
			q_strlcpy (vr_weapon_menu_frame.models[i], copy->model_path,
				sizeof (vr_weapon_menu_frame.models[i]));
			copy->model_path = vr_weapon_menu_frame.models[i];
		}
		vr_weapon_menu_frame.visible[i].entry = copy;
	}
	vr_weapon_menu_frame.outer_radius = layout_radius;
	vr_weapon_menu_frame.action_count = VR_WeaponMenu_BuildActions (catalog,
		VR_WeaponMenu_QuickSaveAvailable (), layout_radius, scale,
		vr_weapon_menu_frame.actions);
	vr_weapon_menu_frame.generation = vr_weapon_menu_session_generation;
	vr_weapon_menu_frame_valid = true;
}

void VR_WeaponMenu_SetVRPanel (const float world_from_ndc[16], qboolean playspace)
{
	if (!world_from_ndc || !vr_weapon_menu_frame_valid ||
		vr_weapon_menu_frame.generation != vr_weapon_menu_session_generation)
		return;
	memcpy (vr_weapon_menu_frame.world_from_ndc, world_from_ndc,
		sizeof (vr_weapon_menu_frame.world_from_ndc));
	vr_weapon_menu_frame.model_yaw = VR_WeaponMenu_ModelYaw (world_from_ndc);
	vr_weapon_menu_frame.playspace = playspace;
	vr_weapon_menu_frame.panel_valid = true;
}

qboolean VR_WeaponMenu_UsesForegroundDepth (void)
{
	return VR_WeaponMenu_IsOpenVR () && vr_weapon_menu_frame_valid &&
		vr_weapon_menu_frame.panel_valid && !vr_weapon_menu_frame.playspace &&
		vr_weapon_menu_frame.generation == vr_weapon_menu_session_generation;
}

static void VR_WeaponMenu_SlotWorldOrigin (const vr_weapon_menu_visible_t *visible,
	const float world_from_ndc[16], const vec3_t right, const vec3_t down,
	const vec3_t forward, float mesh_scale, vec3_t origin)
{
	const vr_weapon_menu_entry_t *entry = visible->entry;
	const float x = visible->center_x * (2.0f / glwidth) - 1.0f;
	const float y = visible->center_y * (2.0f / glheight) - 1.0f;
	for (int axis = 0; axis < 3; ++axis)
		origin[axis] = world_from_ndc[12 + axis] + world_from_ndc[axis] * x +
			world_from_ndc[4 + axis] * y +
			entry->model_offset[0] * mesh_scale * right[axis] -
			entry->model_offset[1] * mesh_scale * down[axis] +
			entry->model_offset[2] * mesh_scale * forward[axis];
}

static qboolean VR_WeaponMenu_PanelBasis (const float world_from_ndc[16],
	vec3_t right, vec3_t down, vec3_t forward)
{
	for (int axis = 0; axis < 3; ++axis)
	{
		right[axis] = world_from_ndc[axis];
		down[axis] = world_from_ndc[4 + axis];
		forward[axis] = world_from_ndc[8 + axis];
	}
	return VectorNormalize (right) > 0.0f && VectorNormalize (down) > 0.0f &&
		VectorNormalize (forward) > 0.0f;
}

static qboolean VR_WeaponMenu_RayBounds (const vec3_t mins, const vec3_t maxs,
	const vec3_t ray_origin, const vec3_t ray_direction, float *distance)
{
	float near_distance = 0.0f;
	float far_distance = FLT_MAX;

	for (int axis = 0; axis < 3; ++axis)
	{
		float first, second;
		if (!isfinite (mins[axis]) || !isfinite (maxs[axis]) ||
			mins[axis] > maxs[axis] || !isfinite (ray_origin[axis]) ||
			!isfinite (ray_direction[axis]))
			return false;
		if (fabsf (ray_direction[axis]) < 1.0e-8f)
		{
			if (ray_origin[axis] < mins[axis] || ray_origin[axis] > maxs[axis])
				return false;
			continue;
		}
		first = (mins[axis] - ray_origin[axis]) / ray_direction[axis];
		second = (maxs[axis] - ray_origin[axis]) / ray_direction[axis];
		if (first > second)
		{
			const float swap = first;
			first = second;
			second = swap;
		}
		near_distance = q_max (near_distance, first);
		far_distance = q_min (far_distance, second);
		if (near_distance > far_distance)
			return false;
	}
	if (!isfinite (near_distance) || far_distance < 0.0f)
		return false;
	*distance = near_distance;
	return true;
}

static qboolean VR_WeaponMenu_WorldContactVisible (const vec3_t start,
	const vec3_t contact)
{
	vec3_t delta, impact, hit_normal, remaining;
	VectorSubtract (contact, start, delta);
	const float distance = VectorLength (delta);
	if (!isfinite (distance) || distance > 128.0f)
		return false;
	if (cl.worldmodel && !cl.worldmodel->needload)
	{
		CL_TraceWorldLine (start, contact, impact, hit_normal);
		VectorSubtract (contact, impact, remaining);
		if (VectorLength (remaining) >= 1.0f)
			return false;
	}
	return true;
}

static int VR_WeaponMenu_HitWorld (const vr_weapon_menu_frame_t *frame,
	const float world_from_ndc[16], const vec3_t ray_origin,
	const vec3_t ray_direction)
{
	vec3_t right, down, forward, direction;
	float best_distance = FLT_MAX;
	const float model_yaw = world_from_ndc ?
		VR_WeaponMenu_ModelYaw (world_from_ndc) : 0.0f;
	const float yaw_radians = DEG2RAD (model_yaw);
	const float yaw_cos = cosf (yaw_radians);
	const float yaw_sin = sinf (yaw_radians);
	const float mesh_scale = VR_WEAPON_MENU_PLAYSPACE_MESH_SCALE;
	int best = -1;
	if (!world_from_ndc || !ray_origin || !ray_direction ||
		!isfinite (model_yaw) ||
		!VR_WeaponMenu_PanelBasis (world_from_ndc, right, down, forward))
		return -1;
	VectorCopy (ray_direction, direction);
	if (VectorNormalize (direction) == 0.0f)
		return -1;
	for (int i = 0; i < frame->count; ++i)
	{
		const qmodel_t *model = frame->model[i];
		const vr_weapon_menu_entry_t *entry = frame->visible[i].entry;
		vec3_t target, local_origin, local_direction, contact;
		float schema_scale, scale_value, layout_scale, entity_scale, world_scale, along;
		float delta_x, delta_y;
		if (!frame->visible[i].selectable || !model || !frame->geometry[i])
			continue;
		schema_scale = isfinite (entry->model_scale) && entry->model_scale > 0.0f ?
			entry->model_scale : 1.0f;
		/* A hit immediately draws this candidate at the selected model scale. */
		scale_value = 0.40f * schema_scale;
		layout_scale = 0.25f * schema_scale * mesh_scale;
		entity_scale = ENTSCALE_DECODE (ENTSCALE_ENCODE (scale_value));
		world_scale = entity_scale * mesh_scale;
		if (!isfinite (layout_scale) || !isfinite (world_scale) || world_scale <= 0.0f)
			continue;
		VR_WeaponMenu_SlotWorldOrigin (&frame->visible[i], world_from_ndc,
			right, down, forward, mesh_scale, target);
		VectorMA (target, 0.5f * (model->mins[2] + model->maxs[2]) *
			layout_scale, down, target);
		if (!isfinite (target[0]) || !isfinite (target[1]) || !isfinite (target[2]))
			continue;
		delta_x = ray_origin[0] - target[0];
		delta_y = ray_origin[1] - target[1];
		local_origin[0] = (yaw_cos * delta_x + yaw_sin * delta_y) / world_scale;
		local_origin[1] = (-yaw_sin * delta_x + yaw_cos * delta_y) / world_scale;
		local_origin[2] = (ray_origin[2] - target[2]) / world_scale;
		delta_x = direction[0];
		delta_y = direction[1];
		local_direction[0] = (yaw_cos * delta_x + yaw_sin * delta_y) / world_scale;
		local_direction[1] = (-yaw_sin * delta_x + yaw_cos * delta_y) / world_scale;
		local_direction[2] = direction[2] / world_scale;
		if (!VR_WeaponMenu_RayBounds (model->mins, model->maxs,
			local_origin, local_direction, &along) || along > 128.0f ||
			along >= best_distance)
			continue;
		VectorMA (ray_origin, along, direction, contact);
		if (!VR_WeaponMenu_WorldContactVisible (ray_origin, contact))
			continue;
		best_distance = along;
		best = i;
	}
	return best;
}

static qboolean VR_WeaponMenu_RetainedWorldVisible (
	const vr_weapon_menu_frame_t *frame, int index,
	const float world_from_ndc[16], const vec3_t ray_origin)
{
	vec3_t right, down, forward, target;
	const vr_weapon_menu_visible_t *visible;
	const qmodel_t *model;
	if (!frame || index < 0 || index >= frame->count || !world_from_ndc ||
		!ray_origin || glwidth <= 0 || glheight <= 0 ||
		!VR_WeaponMenu_PanelBasis (world_from_ndc, right, down, forward))
		return false;
	visible = &frame->visible[index];
	model = frame->model[index];
	if (model && frame->geometry[index])
	{
		const vr_weapon_menu_entry_t *entry = visible->entry;
		const float schema_scale = isfinite (entry->model_scale) &&
			entry->model_scale > 0.0f ? entry->model_scale : 1.0f;
		const float layout_scale = 0.25f * schema_scale *
			VR_WEAPON_MENU_PLAYSPACE_MESH_SCALE;
		VR_WeaponMenu_SlotWorldOrigin (visible, world_from_ndc,
			right, down, forward, VR_WEAPON_MENU_PLAYSPACE_MESH_SCALE, target);
		VectorMA (target, 0.5f * (model->mins[2] + model->maxs[2]) *
			layout_scale, down, target);
	}
	else
	{
		const float x = visible->center_x * (2.0f / glwidth) - 1.0f;
		const float y = visible->center_y * (2.0f / glheight) - 1.0f;
		for (int axis = 0; axis < 3; ++axis)
			target[axis] = world_from_ndc[12 + axis] +
				world_from_ndc[axis] * x + world_from_ndc[4 + axis] * y;
	}
	if (!isfinite (target[0]) || !isfinite (target[1]) || !isfinite (target[2]))
		return false;
	return VR_WeaponMenu_WorldContactVisible (ray_origin, target);
}

static int VR_WeaponMenu_HitWorldFallback (const vr_weapon_menu_frame_t *frame,
	int pointer_x, int pointer_y)
{
	int best = -1;
	float best_distance = FLT_MAX;
	for (int i = 0; i < frame->count; ++i)
	{
		const vr_weapon_menu_visible_t *visible = &frame->visible[i];
		float dx, dy, distance;
		if (!visible->selectable ||
			(frame->model[i] && frame->geometry[i]) ||
			pointer_x < visible->left || pointer_x > visible->left + visible->width ||
			pointer_y < visible->top || pointer_y > visible->top + visible->height)
			continue;
		dx = pointer_x - visible->center_x;
		dy = pointer_y - visible->center_y;
		distance = dx * dx + dy * dy;
		if (distance < best_distance)
		{
			best_distance = distance;
			best = i;
		}
	}
	return best;
}

static void VR_WeaponMenu_ResolvePreparedSelection (qboolean pointer_valid,
	int pointer_x, int pointer_y, const float world_from_ndc[16],
	const float ray_origin[3], const float ray_direction[3], qboolean playspace)
{
	int selected = -1;
	int action = -1;
	const float radius = q_min (glwidth, glheight) * 0.32f;
	const float scale = CLAMP (0.85f,
		q_min (glwidth, glheight) / 720.0f, 1.5f);
	vr_weapon_menu_hover_id = -1;
	vr_weapon_menu_hover_action_slot = -1;
	vr_weapon_menu_hover_action_name[0] = '\0';
	if (vr_weapon_menu_open_vr && playspace)
		selected = VR_WeaponMenu_HitWorld (&vr_weapon_menu_frame,
			world_from_ndc, ray_origin, ray_direction);
	else if (pointer_valid)
		selected = VR_WeaponMenu_Hit (vr_weapon_menu_frame.visible,
			vr_weapon_menu_frame.count, pointer_x, pointer_y, radius);
	if (vr_weapon_menu_open_vr && playspace && selected < 0 && pointer_valid)
		selected = VR_WeaponMenu_HitWorldFallback (&vr_weapon_menu_frame,
			pointer_x, pointer_y);
	if (pointer_valid)
		action = VR_WeaponMenu_Actions (NULL, vr_weapon_menu_frame.actions,
			vr_weapon_menu_frame.action_count, scale,
			pointer_x, pointer_y, false, -1);
	if (action >= 0 && (!playspace || selected < 0))
	{
		const vr_weapon_menu_action_t *hit = &vr_weapon_menu_frame.actions[action];
		vr_weapon_menu_retained_id = -1;
		vr_weapon_menu_hover_id = hit->id;
		if (hit->kind == VR_WEAPON_MENU_ACTION_COOP_PLAYER)
		{
			vr_weapon_menu_hover_action_slot = hit->slot;
			q_strlcpy (vr_weapon_menu_hover_action_name, hit->player_name,
				sizeof (vr_weapon_menu_hover_action_name));
		}
		return;
	}
	if (selected >= 0 && vr_weapon_menu_frame.visible[selected].selectable)
	{
		vr_weapon_menu_retained_id =
			vr_weapon_menu_frame.visible[selected].entry->id;
		vr_weapon_menu_hover_id = vr_weapon_menu_retained_id;
		return;
	}
	for (int i = 0; i < vr_weapon_menu_frame.count; ++i)
		if (vr_weapon_menu_frame.visible[i].entry->id == vr_weapon_menu_retained_id &&
			vr_weapon_menu_frame.visible[i].selectable &&
			(!vr_weapon_menu_open_vr || !playspace ||
			 VR_WeaponMenu_RetainedWorldVisible (&vr_weapon_menu_frame, i,
				world_from_ndc, ray_origin)))
		{
			vr_weapon_menu_hover_id = vr_weapon_menu_retained_id;
			break;
		}
}

/* Return true only when finite model bounds prove the prepared mesh is outside
 * the renderer's active frustum. R_CullBox uses the stereo-union frustum when
 * OpenXR stereo is active. */
static qboolean VR_WeaponMenu_CullPreparedMesh (const qmodel_t *model,
	const aliashdr_t *geometry, const vec3_t origin, float entity_scale,
	float mesh_scale)
{
	vec3_t mins, maxs;
	const double scale = (double)entity_scale * (double)mesh_scale;
	double radius_squared = 0.0;
	double rounding_magnitude = 1.0;

	/* The existing stereo frustum encloses both eyes. MDL bounds cover all
	 * poses, so a sphere about the model origin remains safe under yaw. */
	if (!model || model->type != mod_alias || !geometry || !origin ||
		!isfinite (entity_scale) || entity_scale <= 0.0f ||
		!isfinite (mesh_scale) || mesh_scale <= 0.0f || !isfinite (scale))
		return false;
	/* The shared qmodel bounds can belong to an MDL fallback even when enhanced
	 * geometry is selected. Native MD3 bounds omit later frames, and MD5 applies
	 * animated joints. Keep all of those formats visible until bounds match the
	 * exact selected geometry. */
	if (geometry->poseverttype != PV_QUAKE1 ||
		geometry != (const aliashdr_t *)model->extradata[PV_QUAKE1])
		return false;

	for (int axis = 0; axis < 3; ++axis)
	{
		const double extent = fmax (fabs ((double)model->mins[axis]),
			fabs ((double)model->maxs[axis]));
		double header_magnitude;
		if (!isfinite (origin[axis]) || !isfinite (model->mins[axis]) ||
			!isfinite (model->maxs[axis]) || model->mins[axis] > model->maxs[axis] ||
			!isfinite (geometry->scale_origin[axis]) ||
			!isfinite (geometry->scale[axis]))
			return false;
		radius_squared += extent * extent;
		/* MDL header translation can nearly cancel 8-bit vertex scale in
		 * decoded bounds, yet float matrix construction rounds them apart. */
		header_magnitude = fabs ((double)geometry->scale_origin[axis]) +
			255.0 * fabs ((double)geometry->scale[axis]);
		rounding_magnitude = fmax (rounding_magnitude,
			fabs ((double)origin[axis]) + scale * (extent + header_magnitude));
	}

	/* Convert the rotation-independent sphere to an AABB. Include pre-cancelled
	 * header magnitudes and round endpoints outward for GPU float arithmetic. */
	{
		const double radius = scale * sqrt (radius_squared) +
			0.01 + 128.0 * FLT_EPSILON * rounding_magnitude;
		if (!isfinite (radius) || radius < 0.0)
			return false;
		for (int axis = 0; axis < 3; ++axis)
		{
			const double lower = (double)origin[axis] - radius;
			const double upper = (double)origin[axis] + radius;
			if (!isfinite (lower) || !isfinite (upper) ||
				lower < -FLT_MAX || upper > FLT_MAX)
				return false;
			mins[axis] = nextafterf ((float)lower, -INFINITY);
			maxs[axis] = nextafterf ((float)upper, INFINITY);
			if (!isfinite (mins[axis]) || !isfinite (maxs[axis]))
				return false;
		}
	}

	return R_CullBox (mins, maxs);
}

/* Playspace actions share the prepared UI hit rectangles, but their glyphs
 * belong to the scene so a wall hides them just as it hides wheel meshes. */
static void VR_WeaponMenu_DrawWorldActions (cb_context_t *cbx,
	const vr_weapon_menu_frame_t *frame, const vec3_t right, const vec3_t down)
{
	const float *m = frame->world_from_ndc;
	const float pixel_scale = (2.0f / glwidth) *
		sqrtf (m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
	const float menu_scale = CLAMP (0.85f,
		q_min (glwidth, glheight) / 720.0f, 1.5f);
	const float glyph_size = CHARACTER_SIZE * menu_scale * pixel_scale;
	const float outline = glyph_size / CHARACTER_SIZE;
	const vec3_t black = {0.0f, 0.0f, 0.0f};
	vec3_t up;
	if (!isfinite (glyph_size) || glyph_size <= 0.0f)
		return;
	VectorScale (down, -1.0f, up);
	for (int i = 0; i < frame->action_count; ++i)
	{
		const vr_weapon_menu_action_t *action = &frame->actions[i];
		vec3_t origin, shifted;
		float x, y;
		if (action->width <= 0.0f || action->height <= 0.0f || !action->label[0])
			continue;
		x = (action->left + action->width * 0.5f) * (2.0f / glwidth) - 1.0f;
		y = (action->top + action->height * 0.5f) * (2.0f / glheight) - 1.0f;
		for (int axis = 0; axis < 3; ++axis)
			origin[axis] = m[12 + axis] + m[axis] * x + m[4 + axis] * y;
		if (!isfinite (origin[0]) || !isfinite (origin[1]) || !isfinite (origin[2]))
			continue;
		VectorMA (origin, -outline, right, shifted);
		Draw_String_3DDepth (cbx, shifted, right, up, glyph_size, action->label, black);
		VectorMA (origin, outline, right, shifted);
		Draw_String_3DDepth (cbx, shifted, right, up, glyph_size, action->label, black);
		VectorMA (origin, -outline, up, shifted);
		Draw_String_3DDepth (cbx, shifted, right, up, glyph_size, action->label, black);
		VectorMA (origin, outline, up, shifted);
		Draw_String_3DDepth (cbx, shifted, right, up, glyph_size, action->label, black);
		Draw_String_3DDepth (cbx, origin, right, up, glyph_size, action->label,
			action->color[action->id == vr_weapon_menu_hover_id ? 1 : 0]);
	}
}

int VR_WeaponMenu_DrawModels (struct cb_context_s *context)
{
	cb_context_t *cbx = (cb_context_t *)context;
	const vr_weapon_menu_frame_t *frame = &vr_weapon_menu_frame;
	const float *m = frame->world_from_ndc;
	vec3_t right, down, forward;
	const float mesh_scale = frame->playspace ?
		VR_WEAPON_MENU_PLAYSPACE_MESH_SCALE : 1.0f;
	int aliaspolys = 0;

	if (!cbx || !VR_WeaponMenu_IsOpenVR () || !vr_weapon_menu_frame_valid ||
		!frame->panel_valid || frame->generation != vr_weapon_menu_session_generation ||
		glwidth <= 0 || glheight <= 0)
		return 0;
	if (!VR_WeaponMenu_PanelBasis (m, right, down, forward))
		return 0;

	for (int i = 0; i < frame->count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = frame->visible[i].entry;
		const qmodel_t *model = frame->model[i];
		const float schema_scale = isfinite (entry->model_scale) &&
			entry->model_scale > 0.0f ? entry->model_scale : 1.0f;
		const qboolean selected = entry->id == vr_weapon_menu_hover_id;
		const float entity_scale = (selected ? 0.40f : 0.25f) * schema_scale;
		const float layout_scale = (frame->playspace ? 0.25f :
			(selected ? 0.40f : 0.25f)) * schema_scale * mesh_scale;
		vec3_t tint;
		entity_t entity;

		if (!model || !frame->geometry[i] || !isfinite (entity_scale) ||
			entity_scale <= 0.0f || !isfinite (layout_scale))
			continue;
		memset (&entity, 0, sizeof (entity));
		VR_WeaponMenu_SlotWorldOrigin (&frame->visible[i], m, right, down,
			forward, mesh_scale, entity.origin);
		VectorMA (entity.origin, 0.5f * (model->mins[2] + model->maxs[2]) *
			layout_scale, down, entity.origin);
		if (!isfinite (entity.origin[0]) || !isfinite (entity.origin[1]) ||
			!isfinite (entity.origin[2]))
			continue;
		entity.angles[YAW] = frame->model_yaw;
		entity.model = (qmodel_t *)model;
		entity.colormap = vid.colormap;
		entity.netstate.scale = ENTSCALE_ENCODE (entity_scale);
		entity.alpha = ENTALPHA_ENCODE (1.0f);
		if (selected)
		{
			tint[0] = 0.5f;
			tint[1] = 4.0f;
			tint[2] = 0.5f;
		}
		else if (frame->visible[i].active)
		{
			tint[0] = 4.0f;
			tint[1] = 4.0f;
			tint[2] = 0.0f;
		}
		else
			tint[0] = tint[1] = tint[2] = 1.5f;
		if (!VR_WeaponMenu_CullPreparedMesh (model, frame->geometry[i], entity.origin,
			ENTSCALE_DECODE (entity.netstate.scale), mesh_scale))
			R_DrawPreparedWheelAliasModel (cbx, &entity, frame->geometry[i],
				tint, mesh_scale, &aliaspolys);
		if (frame->playspace && frame->visible[i].ammo >= 0)
		{
			/* The source puts ammo above each mesh in the world. Keep the
			 * draw on the scene path so walls can occlude its glyphs too. */
			char ammo_text[32];
			vec3_t text_origin, up, color = {1.0f, 1.0f, 1.0f};
			const float text_scale = (selected ? 0.20f : 0.15f) * 0.60f;
			const float model_center = 0.5f * (model->mins[2] + model->maxs[2]);
			const float model_top = (model->maxs[2] - model_center) *
				entity_scale * mesh_scale;
			if (frame->visible[i].ammo_max > 0)
				q_snprintf (ammo_text, sizeof (ammo_text), "%d/%d",
					frame->visible[i].ammo, frame->visible[i].ammo_max);
			else
				q_snprintf (ammo_text, sizeof (ammo_text), "%d",
					frame->visible[i].ammo);
			if (frame->visible[i].ammo == 0)
				color[1] = color[2] = 0.0f;
			VectorScale (down, -1.0f, up);
			VectorCopy (entity.origin, text_origin);
			VectorMA (text_origin, -2.0f * 0.60f, forward, text_origin);
			VectorMA (text_origin, model_top + 1.5f * 0.60f, up, text_origin);
			Draw_String_3DDepth (cbx, text_origin, right, up,
				8.0f * text_scale, ammo_text, color);
		}
	}
	if (frame->playspace)
		VR_WeaponMenu_DrawWorldActions (cbx, frame, right, down);
	return aliaspolys;
}

void VR_WeaponMenu_SetDesktopPointer (qboolean pointer_valid,
	int pointer_x, int pointer_y)
{
	const float radius = q_min (glwidth, glheight) * 0.32f;
	const float scale = CLAMP (0.85f,
		q_min (glwidth, glheight) / 720.0f, 1.5f);
	if (!vr_weapon_menu_open || vr_weapon_menu_open_vr)
		return;
	if (!VR_WeaponMenu_SessionValid ())
	{
		VR_WeaponMenu_ClearSession ();
		return;
	}
	vr_weapon_menu_pointer_valid = pointer_valid;
	vr_weapon_menu_pointer_x = pointer_valid ? pointer_x : -1;
	vr_weapon_menu_pointer_y = pointer_valid ? pointer_y : -1;
	VR_WeaponMenu_PrepareFrame (VR_WeaponMenu_CurrentCatalog (), radius, scale,
		NULL, false);
	VR_WeaponMenu_ResolvePreparedSelection (pointer_valid,
		vr_weapon_menu_pointer_x, vr_weapon_menu_pointer_y,
		NULL, NULL, NULL, false);
}

void VR_WeaponMenu_SetVRPointer (qboolean tracking_valid, qboolean pointer_valid,
	int pointer_x, int pointer_y, const float world_from_ndc[16],
	const float ray_origin[3], const float ray_direction[3], qboolean playspace)
{
	int previous_hover_id;
	const float radius = q_min (glwidth, glheight) * 0.32f;
	const float scale = CLAMP (0.85f, q_min (glwidth, glheight) / 720.0f, 1.5f);
	const vr_weapon_menu_catalog_t *catalog;

	if (!VR_WeaponMenu_IsOpenVR ())
		return;
	if (!tracking_valid || !VR_WeaponMenu_SessionValid ())
	{
		VR_WeaponMenu_ClearSession ();
		return;
	}
	vr_weapon_menu_tracking_valid = true;
	vr_weapon_menu_pointer_valid = pointer_valid;
	vr_weapon_menu_pointer_x = pointer_valid ? pointer_x : -1;
	vr_weapon_menu_pointer_y = pointer_valid ? pointer_y : -1;
	previous_hover_id = vr_weapon_menu_hover_id;

	catalog = VR_WeaponMenu_CurrentCatalog ();
	VR_WeaponMenu_PrepareFrame (catalog, radius, scale, world_from_ndc, playspace);
	VR_WeaponMenu_ResolvePreparedSelection (pointer_valid,
		vr_weapon_menu_pointer_x, vr_weapon_menu_pointer_y,
		world_from_ndc, ray_origin, ray_direction, playspace);
	if (vr_weapon_menu_hover_id != -1 && vr_weapon_menu_hover_id != previous_hover_id)
		VR_InputTriggerHaptic (VR_INPUT_ROLE_RIGHT, 0.05f, 0.5f);
}

int VR_WeaponMenu_ReleaseCatalog (const vr_weapon_menu_catalog_t *catalog,
	const int *stats, size_t num_stats, int client_items)
{
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	int count, selected = -1, impulse = 0;

	if (!vr_weapon_menu_open)
		return 0;
	if (!VR_WeaponMenu_SessionValid () || !vr_weapon_menu_frame_valid ||
		vr_weapon_menu_frame.generation != vr_weapon_menu_session_generation ||
		(vr_weapon_menu_open_vr && !vr_weapon_menu_tracking_valid))
	{
		VR_WeaponMenu_ClearSession ();
		return 0;
	}
	count = VR_WeaponMenu_BuildVisible (catalog, stats, num_stats, client_items,
		visible, VR_WEAPON_MENU_MAX_ENTRIES);
	for (int i = 0; i < count; ++i)
		if (visible[i].entry->id == vr_weapon_menu_hover_id)
		{
			selected = i;
			break;
		}
	if (selected >= 0)
	{
		const vr_weapon_menu_entry_t *entry = visible[selected].entry;
		if (visible[selected].selectable && !visible[selected].active &&
			entry->kind == VR_WEAPON_MENU_WEAPON && entry->impulse > 0)
			impulse = entry->impulse;
	}
	VR_WeaponMenu_ClearSession ();
	return impulse;
}

static qboolean VR_WeaponMenu_ActionHoverValid (
	const vr_weapon_menu_action_t *action)
{
	if (!vr_weapon_menu_open_vr)
		return true;
	if (!vr_weapon_menu_tracking_valid || !vr_weapon_menu_pointer_valid ||
		vr_weapon_menu_hover_id != action->id)
		return false;
	return action->kind != VR_WEAPON_MENU_ACTION_COOP_PLAYER ||
		(vr_weapon_menu_hover_action_slot == action->slot &&
		 !strcmp (vr_weapon_menu_hover_action_name, action->player_name));
}

int VR_WeaponMenu_HoveredCoopPlayer (void)
{
	if (!VR_WeaponMenu_IsOpenVR () || !VR_WeaponMenu_SessionValid () ||
		!vr_weapon_menu_frame_valid ||
		vr_weapon_menu_frame.generation != vr_weapon_menu_session_generation)
		return -1;
	for (int index = 0; index < vr_weapon_menu_frame.action_count; ++index)
	{
		const vr_weapon_menu_action_t *action = &vr_weapon_menu_frame.actions[index];
		if (action->kind == VR_WEAPON_MENU_ACTION_COOP_PLAYER &&
			VR_WeaponMenu_ActionHoverValid (action) &&
			VR_WeaponMenu_ActionStillValid (action))
			return action->slot;
	}
	return -1;
}

int VR_WeaponMenu_Release (void)
{
	const vr_weapon_menu_catalog_t *catalog = VR_WeaponMenu_CurrentCatalog ();
	int pointer_x = -1, pointer_y = -1;
	qboolean session_ready;

	if (!vr_weapon_menu_open)
		return 0;
	if (!vr_weapon_menu_open_vr)
	{
		VR_WeaponMenu_Pointer (&pointer_x, &pointer_y);
		VR_WeaponMenu_SetDesktopPointer (pointer_x >= 0 && pointer_y >= 0,
			pointer_x, pointer_y);
	}
	session_ready = vr_weapon_menu_open && VR_WeaponMenu_SessionValid () &&
		vr_weapon_menu_frame_valid &&
		vr_weapon_menu_frame.generation == vr_weapon_menu_session_generation &&
		(!vr_weapon_menu_open_vr || vr_weapon_menu_tracking_valid);

	if (session_ready)
		for (int action = 0; action < vr_weapon_menu_frame.action_count; ++action)
		{
			const vr_weapon_menu_action_t *prepared = &vr_weapon_menu_frame.actions[action];
			char player_command[48];
			const char *command = NULL;
			if (prepared->id != vr_weapon_menu_hover_id ||
				!VR_WeaponMenu_ActionHoverValid (prepared) ||
				!VR_WeaponMenu_ActionStillValid (prepared) ||
				!VR_WeaponMenu_SessionValid ())
				continue;
			switch (prepared->kind)
			{
			case VR_WEAPON_MENU_ACTION_QUICK_SAVE:
				command = "echo Quicksaving...; wait; save quick\n";
				break;
			case VR_WEAPON_MENU_ACTION_QUICK_LOAD:
				command = "echo Quickloading...; wait; load quick\n";
				break;
			case VR_WEAPON_MENU_ACTION_COOP_PLAYER:
				q_snprintf (player_command, sizeof (player_command),
					"coop_teleport_player %d\n", prepared->slot + 1);
				command = player_command;
				break;
			case VR_WEAPON_MENU_ACTION_COOP_SPAWN:
				command = "coop_teleport_spawn\n";
				break;
			default:
				break;
			}
			if (command)
			{
				VR_WeaponMenu_ClearSession ();
				Cbuf_AddText (command);
				return 0;
			}
		}
	return VR_WeaponMenu_ReleaseCatalog (catalog, cl.stats, MAX_CL_STATS, cl.items);
}

void VR_WeaponMenu_DrawCatalog (struct cb_context_s *context,
	const vr_weapon_menu_catalog_t *catalog, const int *stats, size_t num_stats,
	int client_items)
{
	cb_context_t *cbx = (cb_context_t *)context;
	vr_weapon_menu_visible_t local_visible[VR_WEAPON_MENU_MAX_ENTRIES];
	vr_weapon_menu_action_t local_actions[VR_WEAPON_MENU_MAX_ACTIONS];
	vr_weapon_menu_visible_t *visible = local_visible;
	vr_weapon_menu_action_t *actions = local_actions;
	int count, pointer_x, pointer_y, selected, selected_action, action_count;
	const float min_dimension = q_min (glwidth, glheight);
	float outer_radius = min_dimension * 0.32f;
	const float hub_radius = outer_radius * 0.22f;
	const float scale = CLAMP (0.85f, min_dimension / 720.0f, 1.5f);
	char ammo_text[48];

	if (!vr_weapon_menu_open || !cbx || !VR_WeaponMenu_SessionValid ())
		return;

	if (vr_weapon_menu_frame_valid &&
		vr_weapon_menu_frame.generation == vr_weapon_menu_session_generation &&
		catalog == VR_WeaponMenu_CurrentCatalog () && stats == cl.stats &&
		num_stats == MAX_CL_STATS && client_items == cl.items)
	{
		visible = vr_weapon_menu_frame.visible;
		actions = vr_weapon_menu_frame.actions;
		count = vr_weapon_menu_frame.count;
		action_count = vr_weapon_menu_frame.action_count;
		outer_radius = vr_weapon_menu_frame.outer_radius;
	}
	else
	{
		count = VR_WeaponMenu_BuildVisible (catalog, stats, num_stats, client_items,
			visible, VR_WEAPON_MENU_MAX_ENTRIES);
		VR_WeaponMenu_Layout (visible, count, outer_radius, scale);
		action_count = VR_WeaponMenu_BuildActions (catalog,
			VR_WeaponMenu_QuickSaveAvailable (), outer_radius, scale, actions);
	}
	pointer_x = vr_weapon_menu_pointer_x;
	pointer_y = vr_weapon_menu_pointer_y;
	selected = -1;
	if ((!vr_weapon_menu_open_vr || vr_weapon_menu_tracking_valid) &&
		vr_weapon_menu_hover_id != -1)
		for (int i = 0; i < count; ++i)
			if (visible[i].entry->id == vr_weapon_menu_hover_id)
			{
				selected = i;
				break;
			}

	GL_SetCanvas (cbx, CANVAS_DEFAULT);
	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_BLEND);
	if (!vr_weapon_menu_open_vr)
	{
		Draw_Fill (cbx, 0, 0, glwidth, glheight, 0, 0.38f);
		/* The desktop annulus is an opaque UI overlay. A VR wheel draws its
		 * models earlier in the depth-tested scene pass. */
		for (int band = 0; band < 32; ++band)
		{
			const float band_height = 2.0f * outer_radius / 32.0f;
			const float y = -outer_radius + (band + 0.5f) * band_height;
			const float outer_half = sqrtf (q_max (0.0f, outer_radius * outer_radius - y * y));
			if (fabsf (y) < hub_radius)
			{
				const float inner_half = sqrtf (q_max (0.0f, hub_radius * hub_radius - y * y));
				Draw_Fill (cbx, glwidth * 0.5f - outer_half, glheight * 0.5f + y,
					outer_half - inner_half, band_height + 1.0f, 0, 0.78f);
				Draw_Fill (cbx, glwidth * 0.5f + inner_half, glheight * 0.5f + y,
					outer_half - inner_half, band_height + 1.0f, 0, 0.78f);
			}
			else
				Draw_Fill (cbx, glwidth * 0.5f - outer_half, glheight * 0.5f + y,
					2.0f * outer_half, band_height + 1.0f, 0, 0.78f);
		}
		for (int band = 0; band < 12; ++band)
		{
			const float band_height = 2.0f * hub_radius / 12.0f;
			const float y = -hub_radius + (band + 0.5f) * band_height;
			const float half_width = sqrtf (q_max (0.0f, hub_radius * hub_radius - y * y));
			Draw_Fill (cbx, glwidth * 0.5f - half_width, glheight * 0.5f + y,
				2.0f * half_width, band_height + 1.0f, 0, 0.88f);
		}
	}

	for (int i = 0; i < count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = visible[i].entry;
		const qboolean is_selected = i == selected;
		const float icon_scale = scale * 1.25f;
		const float icon_width = 30.0f * icon_scale;
		const float icon_height = 18.0f * icon_scale;
		qpic_t *icon = Sbar_WeaponMenuIcon (entry->selector);
		float label_width = entry->label ? strlen (entry->label) * CHARACTER_SIZE * scale : 0.0f;
		const qboolean has_vr_model = vr_weapon_menu_open_vr &&
			vr_weapon_menu_frame_valid && visible == vr_weapon_menu_frame.visible &&
			vr_weapon_menu_frame.model[i] && vr_weapon_menu_frame.geometry[i];
		if (!has_vr_model)
		{
			Draw_Fill (cbx, visible[i].left, visible[i].top, visible[i].width, visible[i].height,
				is_selected ? 15 : 0, is_selected ? 0.96f : 0.78f);
			if (icon)
				Draw_SubPic (cbx, visible[i].center_x - icon_width * 0.5f,
					visible[i].top + 2.0f * scale, icon_width, icon_height, icon,
					0.0f, 0.0f, 1.0f, 1.0f, NULL, 1.0f);
		}
		GL_SetCanvasColor (is_selected ? (has_vr_model ? 0.55f : 0.05f) :
			(visible[i].selectable ? 1.0f : 0.55f),
			is_selected ? (has_vr_model ? 1.0f : 0.08f) :
			(visible[i].selectable ? 1.0f : 0.55f),
			is_selected ? (has_vr_model ? 0.55f : 0.10f) :
			(visible[i].selectable ? 1.0f : 0.55f), 1.0f);
		if (entry->label && (!vr_weapon_menu_open_vr || !has_vr_model))
			Draw_String_Scaled (cbx, visible[i].center_x - label_width * 0.5f,
				has_vr_model ? visible[i].center_y + 24.0f * scale :
				visible[i].top + 22.0f * scale, entry->label, scale);
		GL_SetCanvasColor (1.0f, 1.0f, 1.0f, 1.0f);
	}
	selected_action = VR_WeaponMenu_Actions (
		vr_weapon_menu_open_vr && vr_weapon_menu_frame_valid &&
		vr_weapon_menu_frame.panel_valid && vr_weapon_menu_frame.playspace ?
		NULL : cbx, actions, action_count, scale,
		pointer_x, pointer_y, true, vr_weapon_menu_hover_id);
	if (selected_action < 0 ||
		vr_weapon_menu_hover_id != actions[selected_action].id)
		selected_action = -1;
	if (selected_action >= 0)
		selected = -1;

	GL_SetCanvasColor (1.0f, 1.0f, 1.0f, 1.0f);
	{
		const qboolean playspace_layout = vr_weapon_menu_open_vr &&
			vr_weapon_menu_frame_valid && vr_weapon_menu_frame.playspace;
		char playspace_info[MAX_QPATH];
		const char *info = selected_action >= 0 ? actions[selected_action].label :
			(selected >= 0 && visible[selected].entry->label ?
			 visible[selected].entry->label : "WEAPON");
		float info_y;
		float info_width;
		float info_x;

		if (playspace_layout)
		{
			VR_WeaponMenu_FitActionLabel (playspace_info, sizeof (playspace_info),
				info, q_max (0.0f, glwidth - 8.0f * scale), scale);
			info = playspace_info;
		}
		info_width = strlen (info) * CHARACTER_SIZE * scale;
		info_x = playspace_layout ? q_max (0.0f,
			q_min (glwidth * 0.5f - info_width * 0.5f, glwidth - info_width)) :
			glwidth * 0.5f - 56.0f * scale;
		info_y = playspace_layout ? q_max (0.0f, q_min (glheight * 0.5f +
			q_max (outer_radius, 28.0f * scale) + 8.0f * scale,
			q_max (0.0f, glheight - 32.0f * scale))) :
			glheight * 0.5f - 12.0f * scale;

		Draw_String_Scaled (cbx, info_x, info_y, info, scale);
		if (selected_action < 0 && selected >= 0 && visible[selected].ammo >= 0)
		{
			q_snprintf (ammo_text, sizeof (ammo_text), "%d / %d",
				visible[selected].ammo, visible[selected].ammo_max);
			Draw_String_Scaled (cbx, glwidth * 0.5f - strlen (ammo_text) * 4.0f * scale,
				playspace_layout ? info_y + 10.0f * scale :
				glheight * 0.5f + 2.0f * scale, ammo_text, scale);
		}
		Draw_String_Scaled (cbx, glwidth * 0.5f - 64.0f * scale,
			playspace_layout ? info_y +
				((selected_action < 0 && selected >= 0 && visible[selected].ammo >= 0) ?
					22.0f : 12.0f) * scale :
			glheight * 0.5f + outer_radius + 10.0f * scale,
			"RELEASE TO SELECT", scale);
	}
	if (!vr_weapon_menu_open_vr || vr_weapon_menu_pointer_valid)
	{
		Draw_Fill (cbx, pointer_x - 5.0f * scale, pointer_y - scale,
			11.0f * scale, 2.0f * scale, 15, 0.95f);
		Draw_Fill (cbx, pointer_x - scale, pointer_y - 5.0f * scale,
			2.0f * scale, 11.0f * scale, 15, 0.95f);
	}
	GL_SetCanvasColor (1.0f, 1.0f, 1.0f, 1.0f);
}

void VR_WeaponMenu_Draw (struct cb_context_s *cbx)
{
	VR_WeaponMenu_DrawCatalog (cbx, VR_WeaponMenu_CurrentCatalog (),
		cl.stats, MAX_CL_STATS, cl.items);
}
