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

#define VR_WEAPON_MENU_MAX_ENTRIES VR_WEAPON_CATALOG_MAX_OBSERVATIONS

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
		STAT_ITEMS, IT_AXE, STAT_ACTIVEWEAPON, IT_AXE, -1, 0, -1, 0, 0},
	{IT_SHOTGUN, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"SHOTGUN", "progs/g_shot.mdl", 1.0f, {0, 0, 0}, IT_SHOTGUN, 2,
		STAT_ITEMS, IT_SHOTGUN, STAT_ACTIVEWEAPON, IT_SHOTGUN,
		STAT_SHELLS, 100, STAT_VR_MAX_SHELLS, 0, 0},
	{IT_SUPER_SHOTGUN, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"DOUBLE SHOTGUN", "progs/g_shot2.mdl", 1.0f, {0, 0, 0}, IT_SUPER_SHOTGUN, 3,
		STAT_ITEMS, IT_SUPER_SHOTGUN, STAT_ACTIVEWEAPON, IT_SUPER_SHOTGUN,
		STAT_SHELLS, 100, STAT_VR_MAX_SHELLS, 0, 0},
	{IT_NAILGUN, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"NAILGUN", "progs/g_nail.mdl", 1.0f, {0, 0, 0}, IT_NAILGUN, 4,
		STAT_ITEMS, IT_NAILGUN, STAT_ACTIVEWEAPON, IT_NAILGUN,
		STAT_NAILS, 200, STAT_VR_MAX_NAILS, 0, 0},
	{IT_SUPER_NAILGUN, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"SUPER NAILGUN", "progs/g_nail2.mdl", 1.0f, {0, 0, 0}, IT_SUPER_NAILGUN, 5,
		STAT_ITEMS, IT_SUPER_NAILGUN, STAT_ACTIVEWEAPON, IT_SUPER_NAILGUN,
		STAT_NAILS, 200, STAT_VR_MAX_NAILS, 0, 0},
	{IT_GRENADE_LAUNCHER, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"GRENADE", "progs/g_rock.mdl", 1.0f, {0, 0, 0}, IT_GRENADE_LAUNCHER, 6,
		STAT_ITEMS, IT_GRENADE_LAUNCHER, STAT_ACTIVEWEAPON, IT_GRENADE_LAUNCHER,
		STAT_ROCKETS, 100, STAT_VR_MAX_ROCKETS, 0, 0},
	{IT_ROCKET_LAUNCHER, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"ROCKET", "progs/g_rock2.mdl", 1.0f, {0, 0, 0}, IT_ROCKET_LAUNCHER, 7,
		STAT_ITEMS, IT_ROCKET_LAUNCHER, STAT_ACTIVEWEAPON, IT_ROCKET_LAUNCHER,
		STAT_ROCKETS, 100, STAT_VR_MAX_ROCKETS, 0, 0},
	{IT_LIGHTNING, VR_WEAPON_MENU_WEAPON, VR_WEAPON_CATALOG_SOURCE_STOCK,
		"LIGHTNING", "progs/g_light.mdl", 1.0f, {0, 0, 0}, IT_LIGHTNING, 8,
		STAT_ITEMS, IT_LIGHTNING, STAT_ACTIVEWEAPON, IT_LIGHTNING,
		STAT_CELLS, 100, STAT_VR_MAX_CELLS, 0, 0}
};

static const vr_weapon_menu_catalog_t vr_weapon_menu_stock_catalog = {
	vr_weapon_menu_stock_entries,
	sizeof (vr_weapon_menu_stock_entries) / sizeof (vr_weapon_menu_stock_entries[0]),
	0
};

static vr_weapon_menu_entry_t vr_weapon_menu_wwheel_entries[VR_WEAPON_MENU_MAX_ENTRIES];
static char vr_weapon_menu_wwheel_labels[VR_WEAPON_MENU_MAX_ENTRIES][32];
static char vr_weapon_menu_schema_models[VR_WEAPON_MENU_MAX_ENTRIES][64];
static int vr_weapon_menu_schema_bitmasks[VR_WEAPON_MENU_MAX_ENTRIES];
static vr_weapon_menu_catalog_t vr_weapon_menu_wwheel_catalog = {
	vr_weapon_menu_wwheel_entries, 0, 1
};
static qboolean vr_weapon_menu_has_wwheel;
static qboolean vr_weapon_menu_has_schema;

static qboolean vr_weapon_menu_open;
static qboolean vr_weapon_menu_open_vr;
static qboolean vr_weapon_menu_pointer_valid;
static qboolean vr_weapon_menu_tracking_valid;
static int vr_weapon_menu_pointer_x, vr_weapon_menu_pointer_y;
static int vr_weapon_menu_hover_id = -1;
static unsigned int vr_weapon_menu_session_generation;
static qmodel_t *vr_weapon_menu_worldmodel;
static int vr_weapon_menu_viewentity;
static char vr_weapon_menu_mapname[sizeof (cl.mapname)];

static const vr_weapon_menu_catalog_t *VR_WeaponMenu_CurrentCatalog (void)
{
	return (vr_weapon_menu_has_wwheel || vr_weapon_menu_has_schema) ?
		&vr_weapon_menu_wwheel_catalog :
		&vr_weapon_menu_stock_catalog;
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

static qboolean VR_WeaponMenu_AddWWheelSlot (int weaponnum, int impulse,
	qboolean have_entvaroffs, int entvaroffs)
{
	vr_weapon_menu_entry_t *entry;
	int stock_index, ammo_stat = -1, ammo_max = 0;

	if (weaponnum <= 0 || impulse <= 0 || impulse > 255 ||
		vr_weapon_menu_wwheel_catalog.count >= VR_WEAPON_MENU_MAX_ENTRIES)
		return false;
	for (size_t i = 0; i < vr_weapon_menu_wwheel_catalog.count; ++i)
		if (vr_weapon_menu_wwheel_entries[i].id == weaponnum)
			return false;

	entry = &vr_weapon_menu_wwheel_entries[vr_weapon_menu_wwheel_catalog.count];
	stock_index = VR_WeaponMenu_FindStockSelector (weaponnum, impulse);
	if (stock_index >= 0)
		*entry = vr_weapon_menu_stock_entries[stock_index];
	else
		memset (entry, 0, sizeof (*entry));

	entry->id = weaponnum;
	entry->kind = VR_WEAPON_MENU_WEAPON;
	entry->source = VR_WEAPON_CATALOG_SOURCE_SCHEMA;
	entry->label = stock_index >= 0 ? vr_weapon_menu_stock_entries[stock_index].label :
		vr_weapon_menu_wwheel_labels[vr_weapon_menu_wwheel_catalog.count];
	entry->selector = stock_index >= 0 ?
		vr_weapon_menu_stock_entries[stock_index].selector : 0;
	entry->impulse = impulse;
	entry->owned_stat = STAT_ITEMS;
	entry->owned_mask = weaponnum;
	entry->active_stat = STAT_ACTIVEWEAPON;
	entry->active_mask = weaponnum;
	entry->ammo_stat = -1;
	entry->ammo_max = 0;
	entry->ammo_max_stat = -1;
	entry->has_schema_peer = 0;
	entry->has_profile_peer = 0;

	if (have_entvaroffs)
		VR_WeaponMenu_WheelAmmo (entvaroffs, &ammo_stat, &ammo_max);
	entry->ammo_stat = ammo_stat;
	entry->ammo_max = ammo_max;
	if (stock_index < 0)
		q_snprintf (vr_weapon_menu_wwheel_labels[
			vr_weapon_menu_wwheel_catalog.count],
			sizeof (vr_weapon_menu_wwheel_labels[0]), "WEAPON %d", weaponnum);

	vr_weapon_menu_wwheel_catalog.count++;
	return true;
}

static qboolean VR_WeaponMenu_SchemaMatchesEntry (
	const vr_weapon_schema_entry_t *schema, const vr_weapon_menu_entry_t *entry)
{
	/* One ownership bit can describe several mod weapon variants. Do not
	 * apply a different impulse's model/ammo metadata to this roster slot. */
	if (schema->impulse > 0 && entry->impulse > 0 &&
		schema->impulse != entry->impulse)
		return false;
	if (schema->active_stat >= 0 && schema->active_mask &&
		entry->active_stat == schema->active_stat &&
		entry->active_mask == schema->active_mask)
		return true;
	if (schema->owned_stat >= 0 && schema->owned_mask &&
		entry->owned_stat == schema->owned_stat &&
		entry->owned_mask == schema->owned_mask)
		return true;
	return schema->bitmask != 0 &&
		entry->active_stat == STAT_ACTIVEWEAPON &&
		entry->active_mask == schema->bitmask;
}

static int VR_WeaponMenu_SchemaStockSelector (
	const vr_weapon_schema_entry_t *schema)
{
	if (schema->bitmask)
		return schema->bitmask;
	if (schema->active_stat == STAT_ACTIVEWEAPON && schema->active_mask)
		return schema->active_mask;
	if (schema->owned_stat == STAT_ITEMS && schema->owned_mask)
		return schema->owned_mask;
	return 0;
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
	const vr_weapon_schema_entry_t *schema, qboolean preserve_roster)
{
	const char *model_path = schema->model_path[0] ? schema->model_path :
		schema->viewmodel_path;

	if (model_path[0])
	{
		q_strlcpy (vr_weapon_menu_schema_models[index], model_path,
			sizeof (vr_weapon_menu_schema_models[index]));
		entry->model_path = vr_weapon_menu_schema_models[index];
		if (entry->label == vr_weapon_menu_wwheel_labels[index])
			VR_WeaponMenu_SchemaLabel (vr_weapon_menu_wwheel_labels[index],
				sizeof (vr_weapon_menu_wwheel_labels[index]), model_path,
				entry->id);
	}
	if (isfinite (schema->scale) && schema->scale > 0.0f)
		entry->model_scale = schema->scale;
	VectorCopy (schema->offset, entry->model_offset);
	if (schema->owned_stat >= 0)
	{
		entry->owned_stat = schema->owned_stat;
		entry->owned_mask = schema->owned_mask;
	}
	else if (!preserve_roster && schema->bitmask)
	{
		/* Legacy schemas use bitmask for STAT_VR_WEAPONS ownership and
		 * STAT_ACTIVEWEAPON equality when no explicit masks are supplied. */
		entry->owned_stat = STAT_VR_WEAPONS;
		entry->owned_mask = schema->bitmask;
		vr_weapon_menu_schema_bitmasks[index] = schema->bitmask;
	}
	if (schema->active_stat >= 0)
	{
		entry->active_stat = schema->active_stat;
		entry->active_mask = schema->active_mask;
	}
	else if (!preserve_roster && schema->bitmask)
	{
		entry->active_stat = -1;
		entry->active_mask = 0;
		entry->selector = schema->bitmask;
	}
	if (schema->ammo_stat >= 0)
	{
		entry->ammo_stat = schema->ammo_stat;
		entry->ammo_max = schema->ammo_max;
	}
	else if (schema->ammo_max > 0)
		entry->ammo_max = schema->ammo_max;
}

static qboolean VR_WeaponMenu_AddSchemaEntry (
	const vr_weapon_schema_entry_t *schema)
{
	const int stock_selector = VR_WeaponMenu_SchemaStockSelector (schema);
	const int stock_index = stock_selector ?
		VR_WeaponMenu_FindStockSelector (stock_selector, schema->impulse) : -1;
	const size_t index = vr_weapon_menu_wwheel_catalog.count;
	vr_weapon_menu_entry_t *entry;

	if (schema->impulse <= 0 || schema->impulse > 255 ||
		(!schema->bitmask && schema->owned_stat < 0 && schema->active_stat < 0) ||
		index >= VR_WEAPON_MENU_MAX_ENTRIES)
		return false;
	for (size_t i = 0; i < index; ++i)
	{
		vr_weapon_menu_entry_t *existing = &vr_weapon_menu_wwheel_entries[i];
		if (existing->source == VR_WEAPON_CATALOG_SOURCE_SCHEMA &&
			existing->impulse == schema->impulse &&
			((schema->bitmask &&
			  (vr_weapon_menu_schema_bitmasks[i] == schema->bitmask ||
			   existing->selector == schema->bitmask)) ||
			 VR_WeaponMenu_SchemaMatchesEntry (schema, existing)))
		{
			VR_WeaponMenu_ApplySchemaMetadata (existing, i, schema, false);
			return true;
		}
	}
	entry = &vr_weapon_menu_wwheel_entries[index];
	if (stock_index >= 0)
		*entry = vr_weapon_menu_stock_entries[stock_index];
	else
		memset (entry, 0, sizeof (*entry));
	entry->id = (int)index + 1;
	entry->kind = VR_WEAPON_MENU_WEAPON;
	entry->source = VR_WEAPON_CATALOG_SOURCE_SCHEMA;
	entry->label = stock_index >= 0 ?
		vr_weapon_menu_stock_entries[stock_index].label :
		vr_weapon_menu_wwheel_labels[index];
	entry->selector = stock_index >= 0 ?
		vr_weapon_menu_stock_entries[stock_index].selector :
		(schema->active_stat < 0 ? schema->bitmask : 0);
	entry->impulse = schema->impulse;
	if (schema->owned_stat < 0 && !schema->bitmask)
	{
		entry->owned_stat = -1;
		entry->owned_mask = 0;
	}
	if (schema->active_stat < 0 && !schema->bitmask && stock_index < 0)
	{
		entry->active_stat = -1;
		entry->active_mask = 0;
	}
	if (stock_index < 0)
		q_snprintf (vr_weapon_menu_wwheel_labels[index],
			sizeof (vr_weapon_menu_wwheel_labels[index]), "WEAPON %d",
			(int)index + 1);
	VR_WeaponMenu_ApplySchemaMetadata (entry, index, schema, false);
	vr_weapon_menu_wwheel_catalog.count++;
	return true;
}

static qboolean VR_WeaponMenu_LoadSchema (
	vr_weapon_schema_entry_t *entries, size_t *count)
{
	byte *data;
	unsigned int path_id = 0;
	qboolean parsed;

	*count = 0;
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
	parsed = VR_WeaponSchemaParse ((const char *)data, entries,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, count);
	Mem_Free (data);
	if (!parsed || !*count)
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
	if (vr_weapon_menu_has_wwheel)
	{
		for (size_t s = 0; s < schema_count; ++s)
		{
			for (size_t i = 0; i < vr_weapon_menu_wwheel_catalog.count; ++i)
			{
				vr_weapon_menu_entry_t *entry =
					&vr_weapon_menu_wwheel_entries[i];
				if (VR_WeaponMenu_SchemaMatchesEntry (&schemas[s], entry))
					VR_WeaponMenu_ApplySchemaMetadata (entry, i, &schemas[s], true);
			}
		}
		return;
	}

	for (size_t s = 0; s < schema_count; ++s)
		if (VR_WeaponMenu_AddSchemaEntry (&schemas[s]))
			vr_weapon_menu_has_schema = true;
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
	size_t schema_count = 0;

	VR_WeaponMenu_Cancel ();
	memset (vr_weapon_menu_wwheel_entries, 0,
		sizeof (vr_weapon_menu_wwheel_entries));
	memset (vr_weapon_menu_wwheel_labels, 0,
		sizeof (vr_weapon_menu_wwheel_labels));
	memset (vr_weapon_menu_schema_models, 0,
		sizeof (vr_weapon_menu_schema_models));
	memset (vr_weapon_menu_schema_bitmasks, 0,
		sizeof (vr_weapon_menu_schema_bitmasks));
	vr_weapon_menu_wwheel_catalog.count = 0;
	vr_weapon_menu_has_wwheel = false;
	vr_weapon_menu_has_schema = false;

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

	if (VR_WeaponMenu_LoadSchema (schema_entries, &schema_count))
		VR_WeaponMenu_ApplySchema (schema_entries, schema_count);
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

static qboolean VR_WeaponMenu_EntryActive (const vr_weapon_menu_entry_t *entry,
	const int *stats, size_t num_stats)
{
	int stat;
	if (entry->active_stat >= 0)
	{
		stat = VR_WeaponMenu_Stat (stats, num_stats, entry->active_stat);
		return entry->active_mask ? (stat & entry->active_mask) != 0 : stat != 0;
	}
	return entry->selector != 0 &&
		VR_WeaponMenu_Stat (stats, num_stats, STAT_ACTIVEWEAPON) == entry->selector;
}

static qboolean VR_WeaponMenu_EntryOwned (const vr_weapon_menu_entry_t *entry,
	const int *stats, size_t num_stats, int client_items, qboolean active)
{
	int stat;
	qboolean owned = false;
	if (entry->owned_stat >= 0)
	{
		stat = entry->owned_stat == STAT_ITEMS ?
			(VR_WeaponMenu_Stat (stats, num_stats, STAT_ITEMS) | client_items) :
			VR_WeaponMenu_Stat (stats, num_stats, entry->owned_stat);
		owned = entry->owned_mask ? (stat & entry->owned_mask) != 0 : stat != 0;
	}
	else if (entry->selector)
		owned = (client_items | VR_WeaponMenu_Stat (stats, num_stats, STAT_ITEMS)) & entry->selector;
	return owned || active;
}

static qboolean VR_WeaponMenu_EntrySelectable (const vr_weapon_menu_entry_t *entry,
	qboolean active, const int *stats, size_t num_stats)
{
	int ammo;
	if (entry->kind != VR_WEAPON_MENU_WEAPON || entry->impulse <= 0)
		return false;
	if (active || entry->ammo_stat < 0 || entry->ammo_stat == STAT_AMMO)
		return true;
	if ((size_t)entry->ammo_stat >= num_stats)
		return true;
	ammo = VR_WeaponMenu_Stat (stats, num_stats, entry->ammo_stat);
	return ammo > 0;
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
		qboolean owned = VR_WeaponMenu_EntryOwned (entry, stats, num_stats, client_items, active);
		if (catalog == &vr_weapon_menu_wwheel_catalog &&
			i < VR_WEAPON_MENU_MAX_ENTRIES && vr_weapon_menu_schema_bitmasks[i])
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
		if (!VR_WeaponCatalog_ShouldExpose (entry->source, catalog->authoritative_schema,
			entry->has_schema_peer, entry->has_profile_peer, owned, active))
			continue;

		visible[count].entry = entry;
		visible[count].active = active;
		visible[count].selectable = VR_WeaponMenu_EntrySelectable (entry, active, stats, num_stats);
		visible[count].ammo = entry->ammo_stat >= 0 ?
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
		const float text_width = visible[i].entry->label ?
			strlen (visible[i].entry->label) * CHARACTER_SIZE * scale : 0.0f;
		const float box_width = q_max (text_width + 12.0f * scale, 40.0f * scale);
		visible[i].center_x = center_x + cosf (angle) * label_radius;
		visible[i].center_y = center_y + sinf (angle) * label_radius;
		visible[i].width = box_width;
		visible[i].height = 44.0f * scale;
		visible[i].left = visible[i].center_x - box_width * 0.5f;
		visible[i].top = visible[i].center_y - visible[i].height * 0.5f;
	}
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

void VR_WeaponMenu_SetVRPointer (qboolean tracking_valid, qboolean pointer_valid,
	int pointer_x, int pointer_y)
{
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	int count, selected = -1;
	const float radius = q_min (glwidth, glheight) * 0.32f;
	const float scale = CLAMP (0.85f, q_min (glwidth, glheight) / 720.0f, 1.5f);

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
	vr_weapon_menu_hover_id = -1;
	if (!pointer_valid)
		return;

	count = VR_WeaponMenu_BuildVisible (VR_WeaponMenu_CurrentCatalog (),
		cl.stats, MAX_CL_STATS, cl.items, visible, VR_WEAPON_MENU_MAX_ENTRIES);
	VR_WeaponMenu_Layout (visible, count, radius, scale);
	selected = VR_WeaponMenu_Hit (visible, count, pointer_x, pointer_y, radius);
	if (selected >= 0 && visible[selected].selectable)
		vr_weapon_menu_hover_id = visible[selected].entry->id;
}

int VR_WeaponMenu_ReleaseCatalog (const vr_weapon_menu_catalog_t *catalog,
	const int *stats, size_t num_stats, int client_items)
{
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	int count, pointer_x = -1, pointer_y = -1, selected = -1, impulse = 0;
	const float radius = q_min (glwidth, glheight) * 0.32f;
	const float scale = CLAMP (0.85f, q_min (glwidth, glheight) / 720.0f, 1.5f);

	if (!vr_weapon_menu_open)
		return 0;
	if (!VR_WeaponMenu_SessionValid ())
	{
		VR_WeaponMenu_ClearSession ();
		return 0;
	}
	if (vr_weapon_menu_open_vr &&
		(!vr_weapon_menu_tracking_valid || !vr_weapon_menu_pointer_valid || vr_weapon_menu_hover_id < 0))
	{
		VR_WeaponMenu_ClearSession ();
		return 0;
	}
	count = VR_WeaponMenu_BuildVisible (catalog, stats, num_stats, client_items,
		visible, VR_WEAPON_MENU_MAX_ENTRIES);
	VR_WeaponMenu_Layout (visible, count, radius, scale);
	if (vr_weapon_menu_open_vr)
	{
		for (int i = 0; i < count; ++i)
			if (visible[i].entry->id == vr_weapon_menu_hover_id)
			{
				selected = i;
				break;
			}
	}
	else
	{
		VR_WeaponMenu_Pointer (&pointer_x, &pointer_y);
		selected = VR_WeaponMenu_Hit (visible, count, pointer_x, pointer_y, radius);
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

int VR_WeaponMenu_Release (void)
{
	return VR_WeaponMenu_ReleaseCatalog (VR_WeaponMenu_CurrentCatalog (),
		cl.stats, MAX_CL_STATS, cl.items);
}

void VR_WeaponMenu_DrawCatalog (struct cb_context_s *context,
	const vr_weapon_menu_catalog_t *catalog, const int *stats, size_t num_stats,
	int client_items)
{
	cb_context_t *cbx = (cb_context_t *)context;
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	int count, pointer_x, pointer_y, selected;
	const float min_dimension = q_min (glwidth, glheight);
	const float outer_radius = min_dimension * 0.32f;
	const float hub_radius = outer_radius * 0.22f;
	const float scale = CLAMP (0.85f, min_dimension / 720.0f, 1.5f);
	char ammo_text[48];

	if (!vr_weapon_menu_open || !cbx || !VR_WeaponMenu_SessionValid ())
		return;

	count = VR_WeaponMenu_BuildVisible (catalog, stats, num_stats, client_items,
		visible, VR_WEAPON_MENU_MAX_ENTRIES);
	VR_WeaponMenu_Layout (visible, count, outer_radius, scale);
	if (vr_weapon_menu_open_vr)
	{
		pointer_x = vr_weapon_menu_pointer_x;
		pointer_y = vr_weapon_menu_pointer_y;
		selected = -1;
		if (vr_weapon_menu_tracking_valid && vr_weapon_menu_pointer_valid)
			for (int i = 0; i < count; ++i)
				if (visible[i].entry->id == vr_weapon_menu_hover_id)
				{
					selected = i;
					break;
				}
	}
	else
	{
		VR_WeaponMenu_Pointer (&pointer_x, &pointer_y);
		selected = VR_WeaponMenu_Hit (visible, count, pointer_x, pointer_y, outer_radius);
	}

	GL_SetCanvas (cbx, CANVAS_DEFAULT);
	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_BLEND);
	Draw_Fill (cbx, 0, 0, glwidth, glheight, 0, 0.38f);

	/* Draw a low-cost annulus from horizontal Vulkan 2D fills; no renderer or
	 * task-graph ownership is added for this desktop-only presentation. */
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

	for (int i = 0; i < count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = visible[i].entry;
		const qboolean is_selected = i == selected;
		const float icon_scale = scale * 1.25f;
		const float icon_width = 30.0f * icon_scale;
		const float icon_height = 18.0f * icon_scale;
		qpic_t *icon = Sbar_WeaponMenuIcon (entry->selector);
		float label_width = entry->label ? strlen (entry->label) * CHARACTER_SIZE * scale : 0.0f;
		Draw_Fill (cbx, visible[i].left, visible[i].top, visible[i].width, visible[i].height,
			is_selected ? 15 : 0, is_selected ? 0.96f : 0.78f);
		if (icon)
			Draw_SubPic (cbx, visible[i].center_x - icon_width * 0.5f,
				visible[i].top + 2.0f * scale, icon_width, icon_height, icon,
				0.0f, 0.0f, 1.0f, 1.0f, NULL, 1.0f);
		GL_SetCanvasColor (is_selected ? 0.05f : (visible[i].selectable ? 1.0f : 0.55f),
			is_selected ? 0.08f : (visible[i].selectable ? 1.0f : 0.55f),
			is_selected ? 0.10f : (visible[i].selectable ? 1.0f : 0.55f), 1.0f);
		if (entry->label)
			Draw_String_Scaled (cbx, visible[i].center_x - label_width * 0.5f,
				visible[i].top + 22.0f * scale, entry->label, scale);
		GL_SetCanvasColor (1.0f, 1.0f, 1.0f, 1.0f);
	}

	GL_SetCanvasColor (1.0f, 1.0f, 1.0f, 1.0f);
	Draw_String_Scaled (cbx, glwidth * 0.5f - 56.0f * scale,
		glheight * 0.5f - 12.0f * scale,
		selected >= 0 && visible[selected].entry->label ? visible[selected].entry->label : "WEAPON", scale);
	if (selected >= 0 && visible[selected].ammo >= 0)
	{
		q_snprintf (ammo_text, sizeof (ammo_text), "%d / %d",
			visible[selected].ammo, visible[selected].ammo_max);
		Draw_String_Scaled (cbx, glwidth * 0.5f - strlen (ammo_text) * 4.0f * scale,
			glheight * 0.5f + 2.0f * scale, ammo_text, scale);
	}
	Draw_String_Scaled (cbx, glwidth * 0.5f - 64.0f * scale,
		glheight * 0.5f + outer_radius + 10.0f * scale,
		"RELEASE TO SELECT", scale);
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
