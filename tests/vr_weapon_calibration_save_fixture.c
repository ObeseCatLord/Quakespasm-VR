/* Exercise the production token-span writer without game or OpenXR startup. */
#include "../Quake/vr_weapon_calibration.c"

#include <assert.h>

static void FixtureInitSlot(void)
{
	VR_WeaponOffsetCvar(0, VR_WOFS_X).value = 9.0f;
	VR_WeaponOffsetCvar(0, VR_WOFS_Y).value = 2.0f;
	VR_WeaponOffsetCvar(0, VR_WOFS_Z).value = 3.0f;
	VR_WeaponOffsetCvar(0, VR_WOFS_SCALE).value = 1.0f;
	VR_WeaponMuzzleCvar(0, VR_WMUZZLE_X).value = 0.0f;
	VR_WeaponMuzzleCvar(0, VR_WMUZZLE_Y).value = 0.0f;
	VR_WeaponMuzzleCvar(0, VR_WMUZZLE_Z).value = 3.0f;
	vr_weapon_calibration_slots[0].has_muzzle_offset = true;
}

static vr_weapon_schema_entry_t FixtureParseOne(const char *text)
{
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	size_t count = 0;
	assert(VR_WeaponSchemaParse(text, entries,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &count));
	assert(count == 1);
	return entries[0];
}

static void FixtureRewrite(const char *source, qboolean enhanced,
	vr_weapon_schema_entry_t *entry)
{
	vr_calibration_textbuf_t output = {0};
	vr_weapon_schema_entry_t original[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	vr_weapon_schema_metadata_t metadata;
	size_t original_count = 0;
	assert(VR_WeaponSchemaParseWithMetadata(source, original,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &original_count, &metadata));
	assert(original_count == 1);
	assert(VR_CalibrationWriteUpdatedBlock(&output, source,
		strlen(source), 0, enhanced, false));
	assert(!strstr(output.data, "mp_held_offset"));
	assert(!strstr(output.data, "mp_muzzle_offset"));
	assert(VR_CalibrationSavedValuesMatch(output.data,
		"progs/v_shot.mdl", 0, enhanced, original_count, &metadata));
	*entry = FixtureParseOne(output.data);
	free(output.data);
}

int main(void)
{
	vr_weapon_schema_entry_t entry;
	vr_calibration_textbuf_t output = {0};
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	size_t count;
	const char *model = "progs/v_shot.mdl";

	FixtureInitSlot();
	FixtureRewrite("{ viewmodel progs/v_shot.mdl held_offset 1 2 3 enhanced_held_offset 4 5 6 }",
		false, &entry);
	assert(entry.has_held_offset && entry.held_offset[0] == 9.0f);
	assert(entry.has_enhanced_held_offset &&
		entry.enhanced_held_offset[0] == 4.0f);
	FixtureRewrite("{ viewmodel progs/v_shot.mdl held_offset 1 2 3 mp_held_offset 4 5 6 enhanced_mp_muzzle_offset 7 8 9 }",
		false, &entry);
	assert(entry.has_held_offset && entry.held_offset[0] == 9.0f);

	FixtureRewrite("{ viewmodel progs/v_shot.mdl held_offset 1 2 3 /* keep */}",
		false, &entry);
	assert(entry.has_held_offset && entry.held_offset[0] == 9.0f);

	vr_weapon_calibration_slots[0].has_enhanced_held_offset = true;
	vr_weapon_calibration_slots[0].enhanced_held_offset[0] = 7.0f;
	vr_weapon_calibration_slots[0].enhanced_held_offset[1] = 8.0f;
	vr_weapon_calibration_slots[0].enhanced_held_offset[2] = 9.0f;
	FixtureRewrite("{ viewmodel progs/v_shot.mdl held_offset 1 2 3 enhanced_held_offset 4 5 6 }",
		true, &entry);
	assert(entry.has_held_offset && entry.held_offset[0] == 1.0f);
	assert(entry.has_enhanced_held_offset &&
		entry.enhanced_held_offset[0] == 7.0f);
	FixtureRewrite("{ viewmodel progs/v_shot.mdl enhanced_held_offset 4 5 6 mp_muzzle_offset 1 2 3 enhanced_mp_held_offset 7 8 9 }",
		true, &entry);
	assert(entry.has_enhanced_held_offset && entry.enhanced_held_offset[0] == 7.0f);

	vr_weapon_calibration_slots[0].has_muzzle_offset = false;
	FixtureRewrite("{ viewmodel progs/v_shot.mdl held_offset 1 2 3 }",
		false, &entry);
	assert(!entry.has_muzzle_offset);

	assert(VR_CalibrationAppendNewBlock(&output, model, 0, false));
	assert(VR_CalibrationTextAppend(&output,
		"global_held_offset 1 2 3\n"
		"{ viewmodel progs/v_nail.mdl held_offset 2 3 4 }\n"));
	assert(VR_WeaponSchemaParse(output.data, entries,
		VR_WEAPON_SCHEMA_MAX_ENTRIES, &count));
	assert(count == 2);
	assert(entries[0].has_held_offset && entries[0].held_offset[0] == 9.0f);
	assert(entries[1].has_held_offset && entries[1].held_offset[0] == 2.0f);
	free(output.data);
	puts("weapon calibration save fixture passed");
	return 0;
}
