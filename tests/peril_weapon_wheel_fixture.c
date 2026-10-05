/* Read-only native VFS, production catalog/schema/release policy; no renderer.
 * The runtime companion qualifies QC selection and actual stat transport. */
#define PERIL_WEAPON_CALIBRATION_FIXTURE
#include "vr_ad_calibration_fixture.c"
#include "../Quake/vr_weapon_menu.c"

client_state_t cl;
client_static_t cls;
viddef_t vid;
vulkanglobals_t vulkan_globals;
server_t sv;
server_static_t svs;
unsigned int d_8to24table[256];
keydest_t key_dest;
qboolean con_forcedup;
int glwidth, glheight;
static qmodel_t fixture_world, fixture_held;
static qpic_t fixture_icon;
static int fixture_icon_selector;
static cb_context_t fixture_context;
static char fixture_draw_labels[32][64];
static size_t fixture_draw_label_count, fixture_draw_icon_count;

/* Observe actual production DrawCatalog descriptor consumption, replacing
 * only GPU primitives. No graphics context or alternative wheel renderer. */
void GL_SetCanvas (cb_context_t *cbx, canvastype canvas) {}
void R_BindGraphicsPipeline (cb_context_t *cbx, graphics_pipeline_t pipeline) {}
void GL_SetCanvasColor (float r, float g, float b, float a) {}
void Draw_Fill (cb_context_t *cbx, float x, float y, float w, float h, int c, float alpha) {}
void Draw_SubPic (cb_context_t *cbx, float x, float y, float w, float h, qpic_t *pic,
	float s1, float t1, float s2, float t2, float *rgb, float alpha)
{
	assert (pic == &fixture_icon);
	++fixture_draw_icon_count;
}
void Draw_String_Scaled (cb_context_t *cbx, float x, float y, const char *text, float scale)
{
	assert (fixture_draw_label_count < countof (fixture_draw_labels));
	q_strlcpy (fixture_draw_labels[fixture_draw_label_count++], text, sizeof (fixture_draw_labels[0]));
}

static qboolean FixtureDrewLabel (const char *label)
{
	for (size_t i = 0; i < fixture_draw_label_count; ++i)
		if (!strcmp (fixture_draw_labels[i], label)) return true;
	return false;
}

void Con_Printf (const char *format, ...)
{
	va_list args;
	va_start (args, format);
	vprintf (format, args);
	va_end (args);
}

const vrxr_frame_t *GL_OpenXRFrame (void)
{
	assert (!vulkan_globals.stereo_active);
	return NULL;
}

int VR_InputDominantPhysicalHand (void)
{
	assert (!vulkan_globals.stereo_active);
	return 0;
}

qpic_t *Sbar_WeaponMenuIcon (int selector)
{
	fixture_icon_selector = selector;
	assert (selector != IT_AXE);
	return &fixture_icon;
}

static const vr_weapon_menu_entry_t *FixtureSlot (int selector)
{
	const vr_weapon_menu_catalog_t *catalog = VR_WeaponMenu_CurrentCatalog ();
	const vr_weapon_menu_entry_t *found = NULL;
	for (size_t i = 0; i < catalog->count; ++i)
		if (catalog->entries[i].selector == selector && catalog->entries[i].game_profile)
		{
			assert (!found);
			found = &catalog->entries[i];
		}
	assert (found);
	return found;
}

static void FixtureSchema (const char *text)
{
	vr_weapon_schema_entry_t rows[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	vr_weapon_schema_metadata_t metadata;
	size_t count = 0;
	assert (VR_WeaponSchemaParseWithMetadata (text, rows, countof (rows), &count, &metadata));
	VR_WeaponMenu_ApplySchema (rows, count);
}

static void FixtureActivate (int selector, const char *model)
{
	cl.stats[STAT_ACTIVEWEAPON] = selector;
	cl.stats[STAT_WEAPON] = 1;
	q_strlcpy (fixture_held.name, model, sizeof (fixture_held.name));
	cl.model_precache[1] = &fixture_held;
}

/* Seed a prepared selection to isolate release inventory revalidation. The
 * native GDB companion uses the actual desktop pointer/hover preparation. */
static int FixtureRelease (int selector)
{
	VR_WeaponMenu_Open ();
	assert (VR_WeaponMenu_IsOpen ());
	vr_weapon_menu_frame_valid = true;
	vr_weapon_menu_frame.generation = VR_WeaponMenu_SessionGeneration ();
	vr_weapon_menu_hover_id = FixtureSlot (selector)->id;
	return VR_WeaponMenu_ReleaseCatalog (VR_WeaponMenu_CurrentCatalog (),
		cl.stats, MAX_CL_STATS, cl.items);
}

static void FixtureRoster (void)
{
	static const int selectors[] = {4096, 1, 2, 4, 8, 16, 32, 64};
	static const char *labels[] = {"QUAKE AXE", "SAWN-OFF SHOTGUN", "DOUBLE-BARRELLED SHOTGUN",
		"NAILGUN", "SUPER NAILGUN", "GRENADE LAUNCHER", "ROCKET LAUNCHER", "THUNDERBOLT"};
	static const char *held[] = {"v_shadaxe0", "v_shot", "v_shot2", "v_nail", "v_nail2", "v_rock", "v_rock2", "v_light"};
	static const char *pickup[] = {"g_axe", "g_shot1", "g_shot2", "g_nail", "g_nail2", "g_rock", "g_rock2", "g_light"};
	vr_weapon_menu_visible_t visible[16];
	VR_WeaponMenu_ReloadGame ();
	const vr_weapon_menu_catalog_t *catalog = VR_WeaponMenu_CurrentCatalog ();
	assert (catalog->authoritative_schema && catalog->count == 8);
	assert (!vr_weapon_menu_has_schema); /* Actual loose offset-only file. */
	cl.stats[STAT_VR_MODITEMS] = 0;
	cl.stats[STAT_ACTIVEWEAPON] = 0;
	cl.stats[STAT_ITEMS] = cl.items = 4096 | 127;
	cl.stats[STAT_SHELLS] = cl.stats[STAT_NAILS] = 200;
	cl.stats[STAT_ROCKETS] = cl.stats[STAT_CELLS] = 100;
	assert (VR_WeaponMenu_BuildVisible (catalog, cl.stats, MAX_CL_STATS, cl.items, visible, 16) == 8);
	for (size_t i = 0; i < countof (selectors); ++i)
	{
		const vr_weapon_menu_entry_t *entry = FixtureSlot (selectors[i]);
		char path[MAX_QPATH];
		assert (entry->id == selectors[i] && entry->impulse == (int)i + 1);
		assert (entry->owned_stat == STAT_ITEMS && entry->owned_mask == selectors[i]);
		assert (entry->active_stat == STAT_ACTIVEWEAPON && entry->active_mask == selectors[i]);
		assert (!entry->schema_fields && !strcmp (VR_WeaponMenu_EntryLabel (entry, cl.stats, MAX_CL_STATS), labels[i]));
		snprintf (path, sizeof (path), "progs/%s.mdl", held[i]);
		assert (!strcmp (VR_WeaponMenu_EntryViewmodel (entry, cl.stats, MAX_CL_STATS), path));
		assert (VR_WeaponMenu_EntryModelMatches (entry, path, cl.stats, MAX_CL_STATS));
		assert (COM_OpenFile (path, NULL, NULL) > 0);
		snprintf (path, sizeof (path), "progs/%s.mdl", pickup[i]);
		assert (!strcmp (VR_WeaponMenu_EntryPreviewPath (entry, cl.stats, MAX_CL_STATS), path));
		assert (COM_OpenFile (path, NULL, NULL) > 0);
		fixture_icon_selector = 0;
		assert ((VR_WeaponMenu_EntryIcon (entry, cl.stats, MAX_CL_STATS) != NULL) == (i != 0 && i != 3));
		assert (fixture_icon_selector == ((i != 0 && i != 3) ? selectors[i] : 0));
		assert (FixtureRelease (selectors[i]) == (int)i + 1);
	}
	assert (FixtureSlot (1)->ammo_max == 200 && FixtureSlot (2)->ammo_max == 200);
	assert (!VR_WeaponMenu_EntryModelMatches (FixtureSlot (4096), "progs/v_shadaxe1.mdl", cl.stats, MAX_CL_STATS));
}

static void FixtureUpgrades (void)
{
	static const struct {int selector, mask; const char *held, *pickup, *label;} upgrades[] = {
		{4096,4096,"progs/v_shadaxe3.mdl","progs/g_shadaxe.mdl","SHADOW AXE"},
		{4096,128,"progs/v_ghook.mdl","progs/g_ghook.mdl","GRAPPLE HOOK"},
		{2,2,"progs/v_shot3.mdl","progs/g_shot3.mdl","WIDOWMAKER SHOTGUN"},
		{64,64,"progs/v_plasma.mdl","progs/g_plasma.mdl","PLASMA GUN"}
	};
	for (size_t i = 0; i < countof (upgrades); ++i)
	{
		const vr_weapon_menu_entry_t *entry = FixtureSlot (upgrades[i].selector);
		const char *base = entry->viewmodel_path;
		for (int pass = 0; pass < 3; ++pass) /* base -> replacement -> base */
		{
			cl.stats[STAT_VR_MODITEMS] = pass == 1 ? upgrades[i].mask : 0;
			const char *model = pass == 1 ? upgrades[i].held : base;
			assert (!strcmp (VR_WeaponMenu_EntryViewmodel (entry, cl.stats, MAX_CL_STATS), model));
			assert (!strcmp (VR_WeaponMenu_EntryPreviewPath (entry, cl.stats, MAX_CL_STATS), pass == 1 ? upgrades[i].pickup : entry->model_path));
			assert (!strcmp (VR_WeaponMenu_EntryLabel (entry, cl.stats, MAX_CL_STATS), pass == 1 ? upgrades[i].label : entry->label));
			FixtureActivate (entry->selector, model);
			assert (VR_WeaponMenu_EntryActive (entry, cl.stats, MAX_CL_STATS));
			size_t before = VR_WeaponMenu_CurrentCatalog ()->count;
			VR_WeaponMenu_ObserveActive ();
			assert (VR_WeaponMenu_CurrentCatalog ()->count == before);
			assert (FixtureRelease (entry->selector) == 0); /* No impulse for active slot. */
			FixtureActivate (1, "progs/v_shot.mdl");
			assert (FixtureRelease (entry->selector) == entry->impulse);
			if (pass == 1) assert (!VR_WeaponMenu_EntryIcon (entry, cl.stats, MAX_CL_STATS));
		}
	}
	cl.stats[STAT_VR_MODITEMS] = 4096 | 128;
	assert (!strcmp (VR_WeaponMenu_EntryViewmodel (FixtureSlot (4096), cl.stats, MAX_CL_STATS), "progs/v_ghook.mdl"));
	cl.stats[STAT_VR_MODITEMS] = 4096 | 128 | 2 | 64;
	cl.stats[STAT_ACTIVEWEAPON] = 0;
	cl.stats[STAT_ITEMS] = cl.items = 0;
	vr_weapon_menu_visible_t visible[16];
	assert (VR_WeaponMenu_BuildVisible (VR_WeaponMenu_CurrentCatalog (), cl.stats, MAX_CL_STATS, 0, visible, 16) == 0);
	assert (FixtureRelease (2) == 0); /* Modifier alone does not own parent. */
}

static void FixtureAmmoAndOverrides (void)
{
	cl.stats[STAT_ITEMS] = cl.items = 4096 | 127;
	FixtureActivate (4096, "progs/v_shadaxe0.mdl");
	for (int ammo = 0; ammo <= 3; ++ammo)
	{
		cl.stats[STAT_SHELLS] = cl.stats[STAT_NAILS] = ammo;
		assert (FixtureRelease (1) == (ammo >= 1 ? 2 : 0));
		assert (FixtureRelease (2) == (ammo >= 2 ? 3 : 0));
		assert (FixtureRelease (4) == (ammo >= 1 ? 4 : 0));
		assert (FixtureRelease (8) == (ammo >= 2 ? 5 : 0));
	}
	cl.stats[STAT_CELLS] = 1;
	/* Explicit wheel keys preserve native schema precedence even though the
	 * replacement preview/held path deliberately differs from native QC. */
	FixtureSchema ("{bitmask 2 impulse 33 owned_stat 15 owned_mask 2 active_stat 10 active_mask 2 "
		"model progs/custom_pickup.mdl viewmodel progs/custom_held.mdl ammo_stat 9 ammo_max 17 scale 0.7 offset 1 2 3}");
	const vr_weapon_menu_entry_t *entry = FixtureSlot (2);
	assert (entry->source == VR_WEAPON_CATALOG_SOURCE_SCHEMA && entry->impulse == 33);
	assert (!strcmp (VR_WeaponMenu_EntryPreviewPath (entry, cl.stats, MAX_CL_STATS), "progs/custom_pickup.mdl"));
	assert (!strcmp (VR_WeaponMenu_EntryViewmodel (entry, cl.stats, MAX_CL_STATS), "progs/custom_held.mdl"));
	assert (entry->ammo_stat == STAT_CELLS && entry->ammo_max == 17 && fabsf (entry->model_scale - .7f) < 1e-6f);
	assert (!VR_WeaponMenu_EntryIcon (entry, cl.stats, MAX_CL_STATS));
	assert (!strcmp (VR_WeaponMenu_EntryLabel (entry, cl.stats, MAX_CL_STATS), "CUSTOM HELD"));
	assert (FixtureRelease (2) == 33);
	FixtureSchema ("{bitmask 2 model progs/other_pickup.mdl}");
	assert (!strcmp (VR_WeaponMenu_EntryLabel (FixtureSlot (2), cl.stats, MAX_CL_STATS), "CUSTOM HELD"));
	FixtureSchema ("{bitmask 2 viewmodel progs/v_plasma.mdl}");
	cl.stats[STAT_VR_MODITEMS] = 2;
	assert (!strcmp (VR_WeaponMenu_EntryLabel (FixtureSlot (2), cl.stats, MAX_CL_STATS), "PLASMA GUN"));
	VR_WeaponMenu_ReloadGame ();
	/* A preview-only overlay does not disable the native upgraded held path. */
	FixtureSchema ("{bitmask 2 model progs/custom_pickup.mdl}");
	cl.stats[STAT_VR_MODITEMS] = 2;
	assert (!strcmp (VR_WeaponMenu_EntryViewmodel (FixtureSlot (2), cl.stats, MAX_CL_STATS), "progs/v_shot3.mdl"));
	assert (!strcmp (VR_WeaponMenu_EntryPreviewPath (FixtureSlot (2), cl.stats, MAX_CL_STATS), "progs/custom_pickup.mdl"));
	VR_WeaponMenu_ReloadGame ();
	/* wwheel's explicit native command survives profile enrichment. */
	vr_weapon_menu_wwheel_catalog.count = 0;
	assert (VR_WeaponMenu_ParseWWheel ("slot {weaponnum 2 impulse 33 entvaroffs 216}"));
	VR_WeaponMenu_LoadBuiltinProfiles ();
	assert (FixtureSlot (2)->impulse == 33);
}

static void FixtureSnapshot (void)
{
	VR_WeaponMenu_ReloadGame ();
	cl.stats[STAT_ITEMS] = cl.items = 4096 | 127;
	cl.stats[STAT_VR_MODITEMS] = 2 | 64 | 4096;
	cl.stats[STAT_SHELLS] = cl.stats[STAT_NAILS] = 20;
	VR_WeaponMenu_Open ();
	VR_WeaponMenu_PrepareFrame (VR_WeaponMenu_CurrentCatalog (), 230.4f, 1.0f, NULL, false);
	vr_weapon_menu_visible_t *shot = NULL, *light = NULL, *axe = NULL;
	for (int i = 0; i < vr_weapon_menu_frame.count; ++i)
	{
		vr_weapon_menu_visible_t *row = &vr_weapon_menu_frame.visible[i];
		if (row->entry->selector == 2) shot = row;
		if (row->entry->selector == 64) light = row;
		if (row->entry->selector == 4096) axe = row;
	}
	assert (shot && light && axe);
	assert (!strcmp (shot->label, "WIDOWMAKER SHOTGUN") && !shot->icon);
	assert (!strcmp (light->label, "PLASMA GUN") && !light->icon);
	assert (!strcmp (axe->label, "SHADOW AXE") && !axe->icon);
	assert (shot->label == shot->entry->label && light->label == light->entry->label);
	const float prepared_width = shot->width;
	/* This independent expected descriptor is deliberately not recomputed
	 * through EntryLabel/EntryIcon after mutable globals change. */
	cl.stats[STAT_VR_MODITEMS] = 128;
	fixture_draw_label_count = fixture_draw_icon_count = 0;
	VR_WeaponMenu_DrawCatalog (&fixture_context, VR_WeaponMenu_CurrentCatalog (), cl.stats, MAX_CL_STATS, cl.items);
	assert (FixtureDrewLabel ("WIDOWMAKER SHOTGUN") && FixtureDrewLabel ("PLASMA GUN") && FixtureDrewLabel ("SHADOW AXE"));
	assert (!FixtureDrewLabel ("DOUBLE-BARRELLED SHOTGUN") && !FixtureDrewLabel ("THUNDERBOLT") && !FixtureDrewLabel ("GRAPPLE HOOK"));
	assert (fixture_draw_icon_count == 4); /* Recomputing with changed stats gives 6. */
	assert (!strcmp (shot->label, "WIDOWMAKER SHOTGUN") && !shot->icon);
	assert (!strcmp (light->label, "PLASMA GUN") && !light->icon);
	assert (!strcmp (axe->label, "SHADOW AXE") && !axe->icon);
	assert (!strcmp (shot->entry->model_path, "progs/g_shot3.mdl"));
	VR_WeaponMenu_SetSlotGeometry (vr_weapon_menu_frame.visible,
		(int)(shot - vr_weapon_menu_frame.visible), 100, 100, 1.0f);
	assert (shot->width == prepared_width);
	/* Custom-catalog fallback must use the supplied stats, even when cl.stats
	 * describe a hook. Layout consumes that same resolved descriptor. */
	int alternate[MAX_CL_STATS];
	memcpy (alternate, cl.stats, sizeof (alternate));
	alternate[STAT_VR_MODITEMS] = 4096 | 2 | 64;
	fixture_draw_label_count = fixture_draw_icon_count = 0;
	VR_WeaponMenu_DrawCatalog (&fixture_context, VR_WeaponMenu_CurrentCatalog (), alternate, MAX_CL_STATS, cl.items);
	assert (FixtureDrewLabel ("SHADOW AXE") && !FixtureDrewLabel ("GRAPPLE HOOK"));
	assert (fixture_draw_icon_count == 4);
	vr_weapon_menu_visible_t fallback[16];
	int count = VR_WeaponMenu_BuildVisible (VR_WeaponMenu_CurrentCatalog (), alternate, MAX_CL_STATS, cl.items, fallback, 16);
	for (int i = 0; i < count; ++i)
		if (fallback[i].entry->selector == 4096)
		{
			assert (!strcmp (fallback[i].label, "SHADOW AXE"));
			VR_WeaponMenu_SetSlotGeometry (fallback, i, 100, 100, 1.0f);
			assert (fallback[i].width == strlen ("SHADOW AXE") * CHARACTER_SIZE + 12);
		}
	VR_WeaponMenu_Cancel ();
	puts ("PERIL_WHEEL_SNAPSHOT_PASS prepared upgrade labels/icons/preview/width survive mutable stats; fallback uses supplied stats");
}

int main (int argc, char **argv)
{
	if (argc != 2) return 77;
	assert (SDL_Init (0));
	Sys_FileInit ();
	registered.value = 1;
	FixtureMountMod (argv[1], "peril3.0");
	strcpy (com_gamedir, "/fixtures/peril3.0");
	native_profile_load = true;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	key_dest = key_game;
	cl.worldmodel = &fixture_world;
	cl.viewentity = 1;
	strcpy (cl.mapname, "start");
	vid.width = glwidth = 1280;
	vid.height = glheight = 720;
	for (int flags = 0; flags < 4; ++flags)
	{
		hipnotic = flags & 1;
		rogue = (flags & 2) != 0;
		FixtureRoster ();
		FixtureUpgrades ();
		FixtureAmmoAndOverrides ();
		FixtureSnapshot ();
	}
	FixtureClearPaths ();
	SDL_Quit ();
	puts ("PERIL_WHEEL_POLICY_PASS native VFS/calibration-only schema, 8 slots/12 identities, parent ownership, upgrades/inverse, release/ammo2, icon fallback, explicit overrides");
	return 0;
}
