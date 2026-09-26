/* Reuse the reload fixture's cvar, file, and schema harness. */
#define main VR_ExistingCalibrationReloadFixtureMain
#include "vr_weapon_calibration_reload_fixture.c"
#undef main

int main(void)
{
	vec3_t muzzle;

	assert(VR_ExistingCalibrationReloadFixtureMain() == 0);

	/* Only the installed Alkaline and LimJam game roots get this model. */
	fixture_file_contents = NULL;
	strcpy(com_gamedir, "/fixtures/id1");
	assert(VR_WeaponCalibrationReloadGame());
	assert(!VR_WeaponCalibrationLookupMuzzle(
		"progs/v_alkaxe20fps.mdl", false, false, muzzle));

	strcpy(com_gamedir, "/fixtures/alk");
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_alkaxe20fps.mdl", 12.0f, 54.0f,
		39.5f, 0.25f, 0.0f, 0.0f, 39.5f);
	assert(!VR_WeaponCalibrationLookupMuzzle(
		"progs/v_alkaxe20fps_vr.mdl", false, false, muzzle));

	/* The installed LimJam file names v_axe.mdl, not v_alkaxe20fps.mdl. */
	strcpy(com_gamedir, "/fixtures/limjam");
	fixture_file_contents = "{ viewmodel progs/v_axe.mdl "
		"held_offset -4 24 37 muzzle_offset 0 0 37 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_alkaxe20fps.mdl", 12.0f, 54.0f,
		39.5f, 0.25f, 0.0f, 0.0f, 39.5f);

	/* An authored entry remains authoritative, then a clean reload restores
	 * the built-in values. */
	fixture_file_contents = "{ viewmodel progs/v_alkaxe20fps.mdl "
		"held_offset 1 2 3 held_scale 0.5 muzzle_offset 4 5 6 }";
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_alkaxe20fps.mdl", 1.0f, 2.0f, 3.0f,
		0.5f, 4.0f, 5.0f, 6.0f);
	fixture_file_contents = NULL;
	assert(VR_WeaponCalibrationReloadGame());
	AssertClassicProfile("progs/v_alkaxe20fps.mdl", 12.0f, 54.0f,
		39.5f, 0.25f, 0.0f, 0.0f, 39.5f);

	strcpy(com_gamedir, "/fixtures/id1");
	assert(VR_WeaponCalibrationReloadGame());
	assert(!VR_WeaponCalibrationLookupMuzzle(
		"progs/v_alkaxe20fps.mdl", false, false, muzzle));
	puts("Alkaline/LimJam calibration fixture passed");
	return 0;
}
