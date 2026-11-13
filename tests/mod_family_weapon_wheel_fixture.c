/* Progs-identity families and wheel profiles against the caller's read-only
 * installed mods (native VFS, production catalog/release policy; no renderer).
 * Every mod is mounted under an unlisted directory name unless a check is
 * explicitly about a directory-keyed profile, so families are proven to come
 * from progs.dat content. Usage: mod-family-wheel /path/to/quakespasm_straight
 * Build: tests/run_mod_family_weapon_wheel.py */
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
static double fixture_time;
static vrxr_frame_t fixture_frame;
static const char *fixture_base;

void Con_Printf (const char *format, ...)
{
	va_list args;
	va_start (args, format);
	vprintf (format, args);
	va_end (args);
}

double Sys_DoubleTime (void)
{
	return fixture_time;
}

const vrxr_frame_t *GL_OpenXRFrame (void)
{
	return vulkan_globals.stereo_active ? &fixture_frame : NULL;
}

int VR_InputDominantPhysicalHand (void)
{
	return 1;
}

/* Release can reach action-row geometry and command dispatch; this fixture
 * never hovers actions, so these primitives must not run. */
void GL_SetCanvasColor (float r, float g, float b, float a) {}
void Draw_Fill (cb_context_t *cbx, float x, float y, float w, float h, int c, float alpha) {}
void Draw_String_Scaled (cb_context_t *cbx, float x, float y, const char *text, float scale) {}
void Cbuf_AddText (const char *text) { assert (0); }
void IN_GetMousePos (int *x, int *y) { *x = *y = -1; }
float CL_TraceWorldLine (vec3_t start, vec3_t end, vec3_t impact, vec3_t normal)
{
	VectorCopy (end, impact);
	normal[0] = normal[1] = normal[2] = 0.0f;
	return 1.0f;
}

qpic_t *Sbar_WeaponMenuIcon (int selector)
{
	return selector >= IT_SHOTGUN && selector <= IT_LIGHTNING ? &fixture_icon : NULL;
}

static void FixtureMount (const char *mod, const char *gamedir)
{
	FixtureMountMod (fixture_base, mod);
	snprintf (com_gamedir, sizeof (com_gamedir), "/fixtures/%s", gamedir);
	native_profile_load = true;
}

static const vr_weapon_menu_entry_t *FixtureRow (int selector, int impulse)
{
	const vr_weapon_menu_catalog_t *catalog = VR_WeaponMenu_CurrentCatalog ();
	const vr_weapon_menu_entry_t *found = NULL;
	for (size_t i = 0; i < catalog->count; ++i)
		if (catalog->entries[i].selector == selector &&
			(!impulse || catalog->entries[i].impulse == impulse))
		{
			assert (!found);
			found = &catalog->entries[i];
		}
	if (!found)
		printf ("missing row selector=%d impulse=%d in %s\n", selector, impulse, com_gamedir);
	assert (found);
	return found;
}

static void FixtureActivate (int selector, const char *model)
{
	cl.stats[STAT_ACTIVEWEAPON] = selector;
	cl.stats[STAT_WEAPON] = 1;
	q_strlcpy (fixture_held.name, model, sizeof (fixture_held.name));
	cl.model_precache[1] = &fixture_held;
}

static int FixtureRelease (const vr_weapon_menu_entry_t *entry)
{
	VR_WeaponMenu_Open ();
	assert (VR_WeaponMenu_IsOpen ());
	vr_weapon_menu_frame_valid = true;
	vr_weapon_menu_frame.generation = VR_WeaponMenu_SessionGeneration ();
	vr_weapon_menu_hover_id = entry->id;
	return VR_WeaponMenu_ReleaseCatalog (VR_WeaponMenu_CurrentCatalog (),
		cl.stats, MAX_CL_STATS, cl.items);
}

static void FixtureReload (void)
{
	assert (VR_WeaponCalibrationReloadGame ());
	VR_WeaponMenu_ReloadGame ();
}

/* Loose files report size 0 without a handle; open one to prove existence. */
static qboolean FixtureFileExists (const char *path)
{
	int handle = -1;
	if (!path)
		return false;
	COM_OpenFile (path, &handle, NULL);
	if (handle < 0)
		return false;
	Sys_FileClose (handle);
	return true;
}

static void FixtureAssertFile (const char *path)
{
	if (!FixtureFileExists (path))
	{
		printf ("missing model %s in %s\n", path ? path : "(null)", com_gamedir);
		assert (0);
	}
}

/* Every profile row's preview and held models exist in the mounted game. */
static void FixtureAssertProfileModels (qboolean strict)
{
	const vr_weapon_menu_catalog_t *catalog = VR_WeaponMenu_CurrentCatalog ();
	for (size_t i = 0; i < catalog->count; ++i)
	{
		const vr_weapon_menu_entry_t *entry = &catalog->entries[i];
		size_t count;
		const vr_weapon_menu_alias_t *aliases;
		if (!entry->game_profile)
			continue;
		/* PrepareModels falls back from a missing pickup to the held mesh,
		 * so the held model is the one that must exist. */
		if (strict)
			FixtureAssertFile (VR_WeaponMenu_EntryViewmodel (entry, cl.stats, MAX_CL_STATS));
		else if (!FixtureFileExists (VR_WeaponMenu_EntryViewmodel (entry, cl.stats, MAX_CL_STATS)))
			printf ("note: %s held %s absent (not shipped by this mod)\n", com_gamedir,
				VR_WeaponMenu_EntryViewmodel (entry, cl.stats, MAX_CL_STATS));
		if (!FixtureFileExists (VR_WeaponMenu_EntryPreviewPath (entry, cl.stats, MAX_CL_STATS)))
			printf ("note: %s preview %s absent, held mesh is used\n", com_gamedir,
				VR_WeaponMenu_EntryPreviewPath (entry, cl.stats, MAX_CL_STATS));
		aliases = VR_WeaponMenu_Aliases (entry, &count);
		for (size_t a = 0; aliases && a < count; ++a)
			if (aliases[a].selector == entry->selector && strstr (aliases[a].held, "/v_") &&
				!FixtureFileExists (aliases[a].held))
				printf ("note: %s variant %s absent\n", com_gamedir, aliases[a].held);
	}
}

static void FixtureStats (int items, int items2, int moditems)
{
	memset (cl.stats, 0, sizeof (cl.stats));
	cl.stats[STAT_HEALTH] = 100;
	cl.stats[STAT_ITEMS] = cl.items = items;
	cl.stats[STAT_VR_ITEMS2] = items2;
	cl.stats[STAT_VR_MODITEMS] = moditems;
	cl.stats[STAT_SHELLS] = cl.stats[STAT_NAILS] = 50;
	cl.stats[STAT_ROCKETS] = cl.stats[STAT_CELLS] = 50;
	FixtureActivate (IT_AXE, "progs/v_axe.mdl");
}

static int FixtureVisible (vr_weapon_menu_visible_t *visible)
{
	return VR_WeaponMenu_BuildVisible (VR_WeaponMenu_CurrentCatalog (), cl.stats,
		MAX_CL_STATS, cl.items, visible, VR_WEAPON_MENU_MAX_ENTRIES);
}

static void FixtureIdentities (void)
{
	static const struct { const char *mod; vr_progs_family_t family; int rerelease; } expected[] = {
		{"ad", VR_PROGS_FAMILY_AD, 0}, {"gibtropolis", VR_PROGS_FAMILY_AD, 0},
		{"hwjam2", VR_PROGS_FAMILY_AD, 0}, {"hwjam4", VR_PROGS_FAMILY_AD, 0},
		{"q30a1024", VR_PROGS_FAMILY_AD, 0}, {"quake_rooftop_jam_v2", VR_PROGS_FAMILY_AD, 0},
		{"tcj", VR_PROGS_FAMILY_AD, 0}, {"ravenkeep", VR_PROGS_FAMILY_AD, 0},
		{"peril3.0", VR_PROGS_FAMILY_AD, 0}, {"mjolnir", VR_PROGS_FAMILY_UNKNOWN, 0},
		{"nyarlathotep", VR_PROGS_FAMILY_DRAKE, 0}, {"something_wicked", VR_PROGS_FAMILY_DRAKE, 0},
		{"Tershibboleth", VR_PROGS_FAMILY_DRAKE, 0}, {"warpspasm", VR_PROGS_FAMILY_QUOTH, 0},
		{"immortal", VR_PROGS_FAMILY_QUOTH, 0}, {"zer", VR_PROGS_FAMILY_ZER, 0},
		{"sacrilege", VR_PROGS_FAMILY_PD_LASER, 0}, {"vr", VR_PROGS_FAMILY_QUAKEVR, 0},
		{"rm1.2", VR_PROGS_FAMILY_UNKNOWN, 0}, {"id1", VR_PROGS_FAMILY_UNKNOWN, 1},
		{"hipnotic", VR_PROGS_FAMILY_UNKNOWN, 1}, {"rogue", VR_PROGS_FAMILY_UNKNOWN, 1},
		{"mg3", VR_PROGS_FAMILY_UNKNOWN, 1}, {"dwellv2p2", VR_PROGS_FAMILY_UNKNOWN, 0},
		{"snack3", VR_PROGS_FAMILY_UNKNOWN, 0}, {"alk", VR_PROGS_FAMILY_UNKNOWN, 0},
		{"enyo", VR_PROGS_FAMILY_UNKNOWN, 0}, {"qbj3", VR_PROGS_FAMILY_UNKNOWN, 0},
	};
	for (size_t i = 0; i < countof (expected); ++i)
	{
		char gamedir[64];
		snprintf (gamedir, sizeof (gamedir), "unlisted-%zu", i);
		FixtureMount (expected[i].mod, gamedir);
		const vr_progs_identity_t *identity = VR_ProgsIdentityCurrent ();
		if (!identity->valid || identity->family != expected[i].family ||
			identity->rerelease != expected[i].rerelease)
			printf ("%s: valid=%d family=%d rerelease=%d\n", expected[i].mod,
				identity->valid, identity->family, identity->rerelease);
		assert (identity->valid && identity->family == expected[i].family &&
			identity->rerelease == expected[i].rerelease);
	}
	/* Truncated/garbage input is refused without reading out of bounds. */
	{
		vr_progs_identity_t identity;
		byte junk[64] = {6};
		assert (!VR_ProgsIdentityParse (junk, 16, &identity) && !identity.valid);
		junk[16] = 0xff; junk[17] = 0xff; junk[18] = 0xff; junk[19] = 0x7f;
		assert (!VR_ProgsIdentityParse (junk, sizeof (junk), &identity));
	}
	puts ("MOD_FAMILY_IDENTITY_PASS progs content identifies AD/Drake/Quoth/Zer/Sacrilege/QuakeVR under unlisted directories");
}

/* M7/L10: an AD fork in another directory gets the AD roster and upgrades. */
static void FixtureADFamily (void)
{
	static const char *const forks[] = {"gibtropolis", "hwjam4", "q30a1024",
		"quake_rooftop_jam_v2", "tcj", "ravenkeep"};
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	for (size_t f = 0; f < countof (forks); ++f)
	{
		FixtureMount (forks[f], forks[f]);
		FixtureReload ();
		assert (VR_WeaponMenu_CurrentCatalog ()->authoritative_schema);
		FixtureStats (IT_AXE | 127, 0, 0);
		assert (FixtureVisible (visible) == 8);
		const vr_weapon_menu_entry_t *axe = FixtureRow (IT_AXE, 1);
		const vr_weapon_menu_entry_t *ssg = FixtureRow (IT_SUPER_SHOTGUN, 3);
		const vr_weapon_menu_entry_t *lg = FixtureRow (IT_LIGHTNING, 8);
		assert (axe->game_profile && ssg->game_profile && lg->game_profile);
		FixtureActivate (IT_AXE, "progs/v_shadaxe0.mdl");
		assert (VR_WeaponMenu_EntryActive (axe, cl.stats, MAX_CL_STATS));
		/* Stat-active with an unclaimed model (Ravenkeep's sword) stays active. */
		FixtureActivate (IT_AXE, "progs/v_longsword.mdl");
		assert (VR_WeaponMenu_EntryActive (axe, cl.stats, MAX_CL_STATS));
		assert (FixtureRelease (axe) == 0);
		FixtureActivate (IT_SHOTGUN, "progs/v_shot.mdl");
		assert (!VR_WeaponMenu_EntryActive (axe, cl.stats, MAX_CL_STATS));
		cl.stats[STAT_VR_MODITEMS] = 2 | 64 | 4096;
		assert (!strcmp (VR_WeaponMenu_EntryLabel (ssg, cl.stats, MAX_CL_STATS), "WIDOWMAKER"));
		assert (!strcmp (VR_WeaponMenu_EntryPreviewPath (ssg, cl.stats, MAX_CL_STATS), "progs/g_shot3.mdl"));
		assert (!strcmp (VR_WeaponMenu_EntryViewmodel (ssg, cl.stats, MAX_CL_STATS), "progs/v_shot3.mdl"));
		assert (!VR_WeaponMenu_EntryIcon (ssg, cl.stats, MAX_CL_STATS));
		assert (!strcmp (VR_WeaponMenu_EntryLabel (lg, cl.stats, MAX_CL_STATS), "PLASMA GUN"));
		assert (!strcmp (VR_WeaponMenu_EntryLabel (axe, cl.stats, MAX_CL_STATS), "SHADOW AXE"));
		FixtureActivate (IT_SUPER_SHOTGUN, "progs/v_shot3.mdl");
		assert (VR_WeaponMenu_EntryActive (ssg, cl.stats, MAX_CL_STATS));
		cl.stats[STAT_VR_MODITEMS] = 0;
		assert (!strcmp (VR_WeaponMenu_EntryLabel (ssg, cl.stats, MAX_CL_STATS), "DOUBLE SHOTGUN"));
		assert (VR_WeaponMenu_EntryIcon (ssg, cl.stats, MAX_CL_STATS));
		FixtureAssertProfileModels (true);
		/* Identical AD meshes share the content-keyed AD calibration and the
		 * AD W_FireSpikes source (self.origin + 16 up, no forward step). */
		{
			vec3_t held, source, angles = {0, 0, 0};
			float scale;
			if (strcmp (forks[f], "ravenkeep")) /* Ravenkeep ships its own v_shot */
			{
				assert (VR_WeaponCalibrationLookupHeld ("progs/v_shot.mdl", false, held, &scale));
				assert (fabsf (held[0] - 1.5f) < 1e-4f && fabsf (held[1] - 1.7f) < 1e-4f &&
					fabsf (held[2] - 17.5f) < 1e-4f && fabsf (scale - 0.33f) < 1e-4f);
			}
			VR_WeaponCalibrationProjectileSourceOffset ("progs/v_nail.mdl", IT_NAILGUN,
				angles, 22.0f, source);
			assert (fabsf (source[0]) < 1e-4f && fabsf (source[2] - 16.0f) < 1e-4f);
			VR_WeaponCalibrationProjectileSourceOffset ("progs/v_rock.mdl", IT_ROCKET_LAUNCHER,
				angles, 22.0f, source);
			assert (fabsf (source[0] - 8.0f) < 1e-4f && fabsf (source[2] - 16.0f) < 1e-4f);
		}
	}
	/* Stock QuakeC keeps the eight-forward nail source. */
	{
		vec3_t source, angles = {0, 0, 0};
		FixtureMount ("id1", "unlisted-stock");
		FixtureReload ();
		VR_WeaponCalibrationProjectileSourceOffset ("progs/v_nail.mdl", IT_NAILGUN,
			angles, 22.0f, source);
		assert (fabsf (source[0] - 8.0f) < 1e-4f);
	}
	/* Mjolnir's AD-derived progs keeps its own directory profile. */
	FixtureMount ("mjolnir", "mjolnir");
	FixtureReload ();
	assert (!VR_WeaponMenu_IsADWeaponGame ());
	puts ("MOD_FAMILY_AD_PASS AD forks: AD roster, stat-active highlight, upgrade labels/models/icons");
}

/* M8: Drake arsenal with class-level HUD bits. */
static void FixtureDrake (void)
{
	static const struct { int bit, impulse; const char *label; } rows[] = {
		{2, 21, "AXE"}, {131072, 31, "MJOLNIR"}, {1, 41, "GRAPPLER"},
		{8, 22, "SHOTGUN"}, {16, 32, "RUBY WAND"}, {64, 23, "DOUBLE SHOTGUN"},
		{32, 24, "NAILGUN"}, {512, 25, "SUPER NAILGUN"}, {4096, 35, "HYPERBLASTER"},
		{256, 26, "GRENADE LAUNCHER"}, {1024, 36, "SIDEWINDER"}, {2048, 27, "ROCKET LAUNCHER"},
		{65536, 37, "MIRV LAUNCHER"}, {16384, 28, "THUNDERBOLT"}, {8192, 38, "PLASMA GUN"},
		{262144, 48, "BURIZA-DO KYANON"},
	};
	static const char *const mods[] = {"nyarlathotep", "something_wicked", "Tershibboleth"};
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	for (size_t m = 0; m < countof (mods); ++m)
	{
		FixtureMount (mods[m], "drake-fork");
		FixtureReload ();
		assert (VR_WeaponMenu_CurrentCatalog ()->authoritative_schema);
		/* Every HUD class owned: all sixteen rows offered, none twice. */
		FixtureStats (127, 0, 0);
		FixtureActivate (0, ""); /* nothing held: every row may be released */
		assert (FixtureVisible (visible) == (int)countof (rows));
		for (size_t i = 0; i < countof (rows); ++i)
		{
			const vr_weapon_menu_entry_t *row = FixtureRow (rows[i].bit, rows[i].impulse);
			assert (!strcmp (row->label, rows[i].label));
			assert (FixtureRelease (row) == rows[i].impulse);
		}
		/* Only the shotgun class: shotgun, wand and melee rows. */
		FixtureStats (IT_SHOTGUN, 0, 0);
		assert (FixtureVisible (visible) == 5);
		/* Same HUD class: the held model picks the active row. */
		FixtureActivate (IT_SHOTGUN, "progs/v_wand.mdl");
		assert (VR_WeaponMenu_EntryActive (FixtureRow (16, 32), cl.stats, MAX_CL_STATS));
		assert (!VR_WeaponMenu_EntryActive (FixtureRow (8, 22), cl.stats, MAX_CL_STATS));
		assert (!VR_WeaponMenu_EntryIcon (FixtureRow (8, 22), cl.stats, MAX_CL_STATS));
		/* Shared "best melee" impulse: the axe row shows the chainsaw held. */
		FixtureActivate (IT_AXE, "progs/v_saw.mdl");
		const vr_weapon_menu_entry_t *axe = FixtureRow (2, 21);
		assert (VR_WeaponMenu_EntryActive (axe, cl.stats, MAX_CL_STATS));
		assert (!strcmp (VR_WeaponMenu_EntryLabel (axe, cl.stats, MAX_CL_STATS), "CHAINSAW"));
		assert (!VR_WeaponMenu_EntryActive (FixtureRow (131072, 31), cl.stats, MAX_CL_STATS));
		size_t before = VR_WeaponMenu_CurrentCatalog ()->count;
		VR_WeaponMenu_ObserveActive ();
		assert (VR_WeaponMenu_CurrentCatalog ()->count == before);
		FixtureActivate (IT_AXE, "progs/v_axe.mdl");
		assert (!strcmp (VR_WeaponMenu_EntryLabel (axe, cl.stats, MAX_CL_STATS), "AXE"));
		FixtureAssertProfileModels (true);
	}
	puts ("MOD_FAMILY_DRAKE_PASS 16 arsenal rows with direct impulses, class ownership, model-resolved highlight");
}

static void FixtureSmallFamilies (void)
{
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	static const char *const quoth[] = {"warpspasm", "immortal"};
	for (size_t m = 0; m < countof (quoth); ++m)
	{
		FixtureMount (quoth[m], "quoth-fork");
		hipnotic = m == 1; /* -quoth raises the hipnotic HUD flag */
		FixtureReload ();
		hipnotic = false;
		FixtureStats (IT_AXE | 127 | 8388608 | 65536, 0, 0);
		assert (FixtureRelease (FixtureRow (8388608, 250)) == 250);
		assert (FixtureRelease (FixtureRow (65536, 251)) == 251);
		assert (!strcmp (FixtureRow (8388608, 250)->label, "PLASMA GUN"));
		assert (FixtureVisible (visible) == 10);
		cl.stats[STAT_VR_ITEMS2] = 8;
		assert (!strcmp (VR_WeaponMenu_EntryLabel (FixtureRow (IT_AXE, 1), cl.stats, MAX_CL_STATS), "HAMMER"));
		FixtureAssertProfileModels (strcmp (quoth[m], "warpspasm") != 0);
	}
	FixtureMount ("zer", "zer-fork");
	FixtureReload ();
	FixtureStats (IT_AXE | 127 | 128 | 8388608, 256, 0);
	assert (FixtureRelease (FixtureRow (8388608, 252)) == 252);
	assert (FixtureRelease (FixtureRow (128, 253)) == 253);
	assert (FixtureRelease (FixtureRow (256, 254)) == 254);
	assert (!strcmp (FixtureRow (IT_SUPER_SHOTGUN, 3)->label, "RIOT CONTROLLER"));
	assert (!strcmp (FixtureRow (IT_NAILGUN, 4)->label, "CHAINGUN"));
	cl.stats[STAT_VR_ITEMS2] = 0;
	assert (FixtureVisible (visible) == 10); /* SRE ownership is items2 only. */
	FixtureAssertProfileModels (true);

	FixtureMount ("sacrilege", "pd-fork");
	FixtureReload ();
	FixtureStats (IT_AXE | 127 | 128, 0, 0);
	assert (FixtureRelease (FixtureRow (128, 225)) == 225);
	FixtureAssertProfileModels (true);

	FixtureMount ("vr", "quakevr-fork");
	FixtureReload ();
	FixtureStats (IT_AXE | 127 | 128 | 8388608, 0, 0);
	assert (FixtureRelease (FixtureRow (128, 226)) == 226);
	assert (FixtureRelease (FixtureRow (8388608, 225)) == 225);
	FixtureAssertProfileModels (true);
	puts ("MOD_FAMILY_EXTRAS_PASS Quoth 250/251 + hammer, Zerstorer 252-254, Sacrilege 225, QuakeVR 225/226");
}

/* M5/M6/M9/L11: directory profiles and wwheel slots. */
static void FixtureDirectoryProfiles (void)
{
	vr_weapon_menu_visible_t visible[VR_WEAPON_MENU_MAX_ENTRIES];
	FixtureMount ("dwellv2p2", "dwellv2p2");
	FixtureReload ();
	FixtureStats (IT_AXE | 127, 0, 4 | 8 | 32);
	assert (FixtureRelease (FixtureRow (IT_SHOTGUN, 0)) == 22);
	assert (FixtureRelease (FixtureRow (512, 0)) == 32);
	FixtureMount ("snack3", "snack3");
	FixtureReload ();
	FixtureStats (IT_AXE | 127, 0, 128);
	assert (FixtureRelease (FixtureRow (IT_SUPER_SHOTGUN, 0)) == 23);
	assert (FixtureRelease (FixtureRow (128, 0)) == 33);

	/* Hipnotic without its wwheel.txt: rerelease progs take 225-227. */
	FixtureMount ("hipnotic", "hipnotic");
	hipnotic = true;
	FixtureReload ();
	assert (FixtureRow (HIT_MJOLNIR, 0)->impulse == 226); /* from wwheel */
	VR_WeaponMenu_ReloadGame ();
	vr_weapon_menu_wwheel_catalog.count = 0;
	vr_weapon_menu_has_wwheel = false;
	VR_WeaponMenu_SeedStock ();
	VR_WeaponMenu_LoadBuiltinProfiles ();
	assert (FixtureRow (HIT_MJOLNIR, 0)->impulse == 226);
	assert (FixtureRow (HIT_LASER_CANNON, 0)->impulse == 225);
	assert (FixtureRow (HIT_PROXIMITY_GUN, 0)->impulse == 227);
	hipnotic = false;

	/* Rogue rerelease wwheel 65-68 keep stock names; split pools gate honestly. */
	FixtureMount ("rogue", "rogue");
	rogue = true;
	FixtureReload ();
	assert (!strcmp (FixtureRow (IT_NAILGUN, 65)->label, "NAILGUN"));
	assert (!strcmp (FixtureRow (IT_SUPER_NAILGUN, 66)->label, "SUPER NAILGUN"));
	assert (!strcmp (FixtureRow (IT_GRENADE_LAUNCHER, 67)->label, "GRENADE"));
	assert (!strcmp (FixtureRow (IT_ROCKET_LAUNCHER, 68)->label, "ROCKET"));
	assert (!strcmp (FixtureRow (IT_NAILGUN, 65)->model_path, "progs/g_nail.mdl"));
	FixtureStats (RIT_AXE | IT_NAILGUN | RIT_LAVA_NAILGUN, 0, 0);
	FixtureActivate (RIT_LAVA_NAILGUN, "progs/v_lava.mdl");
	cl.stats[STAT_NAILS] = 0; /* lava pool loaded and empty */
	assert (FixtureRelease (FixtureRow (IT_NAILGUN, 65)) == 65); /* pool not visible */
	int count = FixtureVisible (visible);
	for (int i = 0; i < count; ++i)
		if (visible[i].entry->selector == IT_NAILGUN)
			assert (visible[i].ammo == -1);
	FixtureActivate (IT_SHOTGUN, "progs/v_shot.mdl");
	cl.stats[STAT_NAILS] = 0; /* ordinary pool loaded and empty */
	assert (FixtureRelease (FixtureRow (IT_NAILGUN, 65)) == 0);
	assert (FixtureRelease (FixtureRow (RIT_LAVA_NAILGUN, 60)) == 60);
	rogue = false;

	/* mg3: the hammer upgrades the axe slot instead of duplicating impulse 1. */
	FixtureMount ("mg3", "mg3");
	FixtureReload ();
	FixtureStats (IT_AXE | 127 | 128, 0, 0);
	count = FixtureVisible (visible);
	for (int i = 0; i < count; ++i)
		for (int j = i + 1; j < count; ++j)
			assert (visible[i].entry->impulse != visible[j].entry->impulse);
	const vr_weapon_menu_entry_t *axe = FixtureRow (IT_AXE, 1);
	assert (!strcmp (VR_WeaponMenu_EntryLabel (axe, cl.stats, MAX_CL_STATS), "HAMMER"));
	FixtureActivate (128, "progs/v_hammer.mdl");
	assert (VR_WeaponMenu_EntryActive (axe, cl.stats, MAX_CL_STATS));
	size_t before = VR_WeaponMenu_CurrentCatalog ()->count;
	VR_WeaponMenu_ObserveActive ();
	assert (VR_WeaponMenu_CurrentCatalog ()->count == before);
	assert (VR_WeaponMenu_CheckDuplicateImpulses () == 0);
	/* Suggestion 4: two visible rows sharing an impulse are reported. */
	vr_weapon_menu_wwheel_catalog.count = 0;
	assert (VR_WeaponMenu_ParseWWheel ("slot {weaponnum 1 impulse 2} slot {weaponnum 2 impulse 2}"));
	FixtureStats (IT_SHOTGUN | IT_SUPER_SHOTGUN, 0, 0);
	assert (VR_WeaponMenu_CheckDuplicateImpulses () == 1);
	puts ("MOD_FAMILY_DIRECTORY_PASS dwell 22/32, snack 23/33, hipnotic 225-227, rogue 65-68 labels/pools, mg3 single axe/hammer row");
}

/* M10: short XR invalidity keeps the wheel and last hover; long closes it. */
static void FixtureTrackingGrace (void)
{
	FixtureMount ("id1", "id1");
	FixtureReload ();
	FixtureStats (IT_AXE | 127, 0, 0);
	vulkan_globals.stereo_active = true;
	memset (&fixture_frame, 0, sizeof (fixture_frame));
	fixture_frame.should_render = fixture_frame.focused = 1;
	fixture_frame.devices[0].valid = fixture_frame.devices[0].tracked = 1;
	fixture_frame.devices[0].kind = VRXR_DEVICE_HEAD;
	fixture_frame.devices[0].hand = -1;
	fixture_frame.devices[2].valid = fixture_frame.devices[2].tracked = 1;
	fixture_frame.devices[2].kind = VRXR_DEVICE_HAND;
	fixture_frame.devices[2].hand = 1;
	const vr_weapon_menu_entry_t *shotgun = FixtureRow (IT_SHOTGUN, 2);
	for (int scenario = 0; scenario < 3; ++scenario)
	{
		fixture_time = 100.0 * (scenario + 1);
		VR_WeaponMenu_Open ();
		assert (VR_WeaponMenu_IsOpenVR ());
		vr_weapon_menu_frame_valid = true;
		vr_weapon_menu_frame.generation = VR_WeaponMenu_SessionGeneration ();
		vr_weapon_menu_tracking_valid = true;
		vr_weapon_menu_last_tracked_time = fixture_time;
		vr_weapon_menu_hover_id = shotgun->id;
		/* Hand tracking drops (and the runtime unfocuses) for a while. */
		fixture_frame.devices[2].tracked = 0;
		fixture_time += 0.1;
		assert (VR_WeaponMenu_TrackingLost ());
		if (scenario == 0)
		{
			/* Release 100 ms later: last valid hover (100 ms old) still selects. */
			assert (VR_WeaponMenu_Release () == 2);
		}
		else if (scenario == 1)
		{
			/* 220 ms later: still open (under the 250 ms grace) but the hover is
			 * too old to act on; release selects nothing. */
			fixture_time += 0.12;
			assert (VR_WeaponMenu_TrackingLost () && VR_WeaponMenu_IsOpen ());
			assert (VR_WeaponMenu_Release () == 0);
		}
		else
		{
			/* Sustained loss beyond 250 ms cancels. */
			fixture_time += 0.3;
			assert (!VR_WeaponMenu_TrackingLost () && !VR_WeaponMenu_IsOpen ());
		}
		assert (!VR_WeaponMenu_IsOpen ());
		fixture_frame.devices[2].tracked = 1;
	}
	vulkan_globals.stereo_active = false;
	puts ("MOD_FAMILY_TRACKING_PASS 100 ms blip keeps hover, stale hover not used, sustained loss cancels");
}

int main (int argc, char **argv)
{
	if (argc != 2) return 77;
	fixture_base = argv[1];
	setvbuf (stdout, NULL, _IONBF, 0);
	assert (SDL_Init (0));
	Sys_FileInit ();
	registered.value = 1;
	cls.state = ca_connected;
	cls.signon = SIGNONS;
	key_dest = key_game;
	cl.worldmodel = &fixture_world;
	cl.viewentity = 1;
	strcpy (cl.mapname, "start");
	vid.width = glwidth = 1280;
	vid.height = glheight = 720;
	FixtureIdentities ();
	FixtureADFamily ();
	FixtureDrake ();
	FixtureSmallFamilies ();
	FixtureDirectoryProfiles ();
	FixtureTrackingGrace ();
	FixtureClearPaths ();
	SDL_Quit ();
	puts ("MOD_FAMILY_WHEEL_PASS");
	return 0;
}
