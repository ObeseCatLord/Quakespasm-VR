#define PERIL_WEAPON_CALIBRATION_FIXTURE
#include "vr_ad_calibration_fixture.c"
int main (int argc, char **argv)
{
	if (argc != 2)
		return 77;
	const char *base = argv[1];
	assert (SDL_Init (0));
	Sys_FileInit ();
	registered.value = 1;
	FixtureMountMod (base, "peril3.0");
	strcpy (com_gamedir, "/fixtures/peril3.0");
	fixture_file_contents = NULL;
	assert (VR_WeaponCalibrationReloadGame ());
	fixture_profile_t profiles[countof (fixture_peril_landmarks)];
	for (int i = 0; i < (int)countof (fixture_peril_landmarks); i++)
	{
		char path[MAX_QPATH];
		snprintf (path, sizeof (path), "progs/%s.mdl", fixture_peril_landmarks[i].name);
		assert (VR_WeaponCalibrationLookupHeld (path, false, profiles[i].held, &profiles[i].scale));
		assert (VR_WeaponCalibrationLookupMuzzle (path, false, profiles[i].muzzle));
		assert (profiles[i].scale > 0);
	}
	native_profile_load = true;
	assert (VR_WeaponCalibrationReloadGame ());
	FixtureAssertAllPerilProfiles ();
	for (int i = 0; i < (int)countof (fixture_peril_landmarks); i++)
	{
		char path[MAX_QPATH];
		snprintf (path, sizeof (path), "progs/%s.mdl", fixture_peril_landmarks[i].name);
		AssertClassicProfile (
			path, profiles[i].held[0], profiles[i].held[1], profiles[i].held[2], profiles[i].scale, profiles[i].muzzle[0], profiles[i].muzzle[1],
			profiles[i].muzzle[2]);
	}
	native_profile_load = false;
	/* Exact generic copy is suppressed; one changed or missing field is authored. */
	const char *generic = "{viewmodel progs/v_nail.mdl held_offset -5 3 15 held_scale 0.5 muzzle_offset 0 0 15}";
	fixture_file_contents = generic;
	assert (VR_WeaponCalibrationReloadGame ());
	AssertClassicProfile (
		"progs/v_nail.mdl", profiles[6].held[0], profiles[6].held[1], profiles[6].held[2], .5f, profiles[6].muzzle[0], profiles[6].muzzle[1],
		profiles[6].muzzle[2]);
	fixture_file_contents = "{viewmodel progs/v_nail.mdl held_offset -5 3 15 held_scale 0.5 muzzle_offset 0 0 15.1}";
	assert (VR_WeaponCalibrationReloadGame ());
	AssertClassicProfile ("progs/v_nail.mdl", -5, 3, 15, .5f, 0, 0, 15.1f);
	fixture_file_contents = "{viewmodel progs/v_nail.mdl held_offset -5 3 15 held_scale 0.5}";
	assert (VR_WeaponCalibrationReloadGame ());
	vec3_t held;
	float  scale;
	assert (VR_WeaponCalibrationLookupHeld ("progs/v_nail.mdl", false, held, &scale));
	AssertVector (held, -5, 3, 15);
	fixture_file_contents = "{viewmodel progs/v_shot.mdl held_scale 0.4}";
	assert (VR_WeaponCalibrationReloadGame ());
	assert (VR_WeaponCalibrationLookupHeld ("progs/v_shot.mdl", false, held, &scale));
	assert (fabsf (scale - .4f) < 1e-6);
	AssertVector (held, profiles[3].held[0], profiles[3].held[1], profiles[3].held[2]);
	fixture_file_contents = NULL;
	FixtureMountMod (base, "id1");
	assert (VR_WeaponCalibrationReloadGame ());
	AssertStockClassicProfiles ();
	FixtureClearPaths ();
	SDL_Quit ();
	puts ("PERIL_CALIBRATION_PASS all 20 rows, native VFS/installed profile, exact legacy filtering, changed/partial overrides, stock regression");
	return 0;
}
