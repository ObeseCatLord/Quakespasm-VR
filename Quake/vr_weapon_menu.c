/*
 * Desktop screen-space weapon wheel. Catalog policy is shared with the
 * inherited QuakeSpasm OpenVR implementation; this adapter reads vkQuake's
 * existing client stats and records only transient UI hover identity.
 */
#include "quakedef.h"
#include "vr_input.h"
#include "vr_weapon_menu.h"

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
		const qboolean owned = VR_WeaponMenu_EntryOwned (entry, stats, num_stats, client_items, active);
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

	count = VR_WeaponMenu_BuildVisible (&vr_weapon_menu_stock_catalog,
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
	return VR_WeaponMenu_ReleaseCatalog (&vr_weapon_menu_stock_catalog,
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
	VR_WeaponMenu_DrawCatalog (cbx, &vr_weapon_menu_stock_catalog,
		cl.stats, MAX_CL_STATS, cl.items);
}
