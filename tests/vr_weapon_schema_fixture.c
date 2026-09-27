#include "quakedef.h"
#include "vr_weapon_schema.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void expect_rejected(const char *text, size_t capacity)
{
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	size_t count = 123;

	assert(!VR_WeaponSchemaParse(text, entries, capacity, &count));
	assert(count == 0);
}

static void test_installed_id1_schema(void)
{
	static const char text[] =
		"{ bitmask 1 model progs/v_axe.mdl impulse 1 scale 1 "
		"offset 0 0 0 owned_stat items owned_mask 4096 }\n"
		"{ bitmask 2 model progs/v_shot.mdl impulse 2 scale 1 "
		"offset 0 0 0 ammo shells }\n";
	vr_weapon_schema_entry_t entries[4];
	size_t count = 0;

	assert(VR_WeaponSchemaParse(text, entries, 4, &count));
	assert(count == 2);
	assert(entries[0].bitmask == 1);
	assert(!strcmp(entries[0].model_path, "progs/v_axe.mdl"));
	assert(entries[0].offset[0] == 0 && entries[0].has_offset);
	assert(entries[0].owned_stat == STAT_ITEMS && entries[0].owned_mask == 4096);
	assert(entries[0].scale == 1 && entries[0].held_scale == 1);
	assert(entries[1].bitmask == 2 && entries[1].impulse == 2);
	assert(entries[1].ammo_stat == STAT_SHELLS && entries[1].ammo_max == 100);
}

static void test_qbj3_ad_schema(void)
{
	static const char text[] =
		"global_held_scale 0.25\n"
		"global_held_offset 1 2 3\n"
		"global_muzzle_offset 4 5 6\n"
		"global_mp_held_offset 0.1 0.2 0.3\n"
		"global_mp_muzzle_offset -1 -2 -3\n"
		"{ held_model progs/v_mod.mdl impulse 42 scale 0.75 "
		"offset 7 8 9 held_scale 0.5 held_offset 2 3 4 "
		"mp_held_offset 1 1 1 muzzle_offset 5 6 7 "
		"mp_muzzle_offset 2 2 2 enhanced_held_offset 9 8 7 "
		"enhanced_mp_held_offset 6 5 4 enhanced_muzzle_offset 3 2 1 "
		"enhanced_mp_muzzle_offset 0.5 0.6 0.7 muzzle_source_offset 8 7 6 "
		"muzzle_source_viewofs 1 muzzle_spawn_at_self_origin 1 "
		"active_stat weapons2 active_mask 16 ammo rockets "
		"unknown_setting ignored }\n"
		"global_held_scale 0.3\n"
		"{ bitmask 8 model progs/v_second.mdl enhanced_muzzle_offset 1 2 3 "
		"projectile_spawn_at_self_origin 0 owned_stat moditems owned_mask 32 "
		"ammo_stat cells ammo_max 77 }\n";
	vr_weapon_schema_entry_t entries[4];
	size_t count = 0;

	assert(VR_WeaponSchemaParse(text, entries, 4, &count));
	assert(count == 2);
	assert(!strcmp(entries[0].model_path, "progs/v_mod.mdl"));
	assert(!strcmp(entries[0].viewmodel_path, "progs/v_mod.mdl"));
	assert(entries[0].impulse == 42 && entries[0].scale == 0.75f);
	assert(entries[0].held_scale == 0.5f && entries[0].held_offset[0] == 2);
	assert(entries[0].mp_held_offset[0] == 1.1f);
	assert(entries[0].schema_mp_held_offset[0] == 1);
	assert(entries[0].mp_muzzle_offset[1] == 0);
	assert(entries[0].schema_mp_muzzle_offset[0] == 2);
	assert(entries[0].has_enhanced_held_offset && entries[0].enhanced_held_offset[2] == 7);
	assert(entries[0].has_enhanced_mp_held_offset && entries[0].enhanced_mp_held_offset[0] == 6);
	assert(entries[0].has_enhanced_muzzle_offset && entries[0].enhanced_muzzle_offset[0] == 3);
	assert(entries[0].has_enhanced_mp_muzzle_offset && entries[0].enhanced_mp_muzzle_offset[2] == 0.7f);
	assert(entries[0].has_muzzle_source_offset && entries[0].muzzle_source_offset[1] == 7);
	assert(entries[0].has_muzzle_source_viewofs && entries[0].muzzle_source_viewofs);
	assert(entries[0].spawn_at_self_origin && entries[0].has_spawn_at_self_origin);
	assert(entries[0].owned_stat == STAT_VR_WEAPONS2 && entries[0].owned_mask == 16);
	assert(entries[0].active_stat == STAT_VR_WEAPONS2 && entries[0].active_mask == 16);
	assert(entries[0].ammo_stat == STAT_ROCKETS && entries[0].ammo_max == 100);
	assert(entries[1].held_scale == 0.3f);
	assert(!strcmp(entries[1].viewmodel_path, "progs/v_second.mdl"));
	assert(entries[1].has_enhanced_muzzle_offset);
	assert(!entries[1].spawn_at_self_origin && entries[1].has_spawn_at_self_origin);
	assert(entries[1].owned_stat == STAT_VR_MODITEMS && entries[1].owned_mask == 32);
	assert(entries[1].ammo_stat == STAT_CELLS && entries[1].ammo_max == 77);
}

static void test_aliases_and_stat_names(void)
{
	static const char text[] =
		"{ owned_stat ammo owned_mask 1 }"
		"{ owned_stat shells owned_mask 1 }"
		"{ owned_stat nails owned_mask 1 }"
		"{ owned_stat rockets owned_mask 1 }"
		"{ owned_stat cells owned_mask 1 }"
		"{ owned_stat activeweapon owned_mask 1 }"
		"{ owned_stat items owned_mask 1 }"
		"{ owned_stat weapon owned_mask 1 }"
		"{ owned_stat weapons owned_mask 1 }"
		"{ owned_stat items2 owned_mask 1 }"
		"{ owned_stat moditems owned_mask 1 }"
		"{ owned_stat weapon2 owned_mask 1 }"
		"{ owned_stat weapons2 owned_mask 1 }"
		"{ owned_stat 42 owned_mask 1 }"
		"{ bitmask 1 model progs/a.mdl spawn_at_self_origin 0 }"
		"{ bitmask 1 model progs/b.mdl muzzle_spawn_at_self_origin 0 }"
		"{ bitmask 1 model progs/c.mdl projectile_spawn_at_self_origin 0 }";
	const int expected[] = {
		STAT_AMMO, STAT_SHELLS, STAT_NAILS, STAT_ROCKETS, STAT_CELLS,
		STAT_ACTIVEWEAPON, STAT_ITEMS, STAT_WEAPON, STAT_VR_WEAPONS,
		STAT_VR_ITEMS2, STAT_VR_MODITEMS, STAT_VR_WEAPON2, STAT_VR_WEAPONS2, 42
	};
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	size_t count = 0;
	size_t i;

	assert(VR_WeaponSchemaParse(text, entries, VR_WEAPON_SCHEMA_MAX_ENTRIES, &count));
	assert(count == sizeof(expected) / sizeof(expected[0]) + 3);
	for (i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
		assert(entries[i].owned_stat == expected[i]);
	assert(entries[14].has_spawn_at_self_origin && !entries[14].spawn_at_self_origin);
	assert(entries[15].has_spawn_at_self_origin && !entries[15].spawn_at_self_origin);
	assert(entries[16].has_spawn_at_self_origin && !entries[16].spawn_at_self_origin);
}

static void test_multiplayer_only_offsets_do_not_create_weapon(void)
{
	static const char text[] =
		"global_mp_held_offset 1 2 3\n"
		"global_mp_muzzle_offset 4 5 6\n"
		"{ model progs/mp_only.mdl mp_held_offset 7 8 9 "
		"enhanced_mp_muzzle_offset 2 3 4 }\n"
		"{ model progs/shared.mdl held_offset 1 2 3 }\n";
	vr_weapon_schema_entry_t entries[2];
	size_t count = 0;

	assert(VR_WeaponSchemaParse(text, entries, 2, &count));
	assert(count == 1);
	assert(!strcmp(entries[0].model_path, "progs/shared.mdl"));
	assert(!strcmp(entries[0].viewmodel_path, "progs/shared.mdl"));
	assert(entries[0].has_held_offset);
}

static void test_rejections_and_bounds(void)
{
	char input[16384];
	char overlong[80];
	vr_weapon_schema_entry_t entries[VR_WEAPON_SCHEMA_MAX_ENTRIES];
	size_t count;
	int i;
	int used;

	expect_rejected("{ bitmask 1 offset 1 2 }", 4);
	expect_rejected("{ bitmask 1 scale NaN }", 4);
	expect_rejected("{ bitmask 1 scale 1e999 }", 4);
	expect_rejected("{ bitmask 1 offset 0 inf 0 }", 4);
	expect_rejected("{ bitmask nope }", 4);
	expect_rejected("{ bitmask 1", 4);
	expect_rejected("}", 4);
	expect_rejected("{ bitmask 1 owned_mask 2147483648 }", 4);
	expect_rejected("{ bitmask 1 owned_stat 999 }", 4);
	expect_rejected("global_held_offset 1 2", 4);
	expect_rejected("global_mp_held_offset 3.4e38 0 0 "
				 "{ bitmask 1 mp_held_offset 3.4e38 0 0 }", 4);
	expect_rejected("{ bitmask 1 } { bitmask 2 }", 1);

	memset(overlong, 'k', sizeof(overlong) - 1);
	overlong[sizeof(overlong) - 1] = '\0';
	used = snprintf(input, sizeof(input), "{ %s value }", overlong);
	assert(used > 0 && (size_t)used < sizeof(input));
	expect_rejected(input, 4);
	memset(overlong, 'p', 64);
	overlong[64] = '\0';
	used = snprintf(input, sizeof(input), "{ model %s bitmask 1 }", overlong);
	assert(used > 0 && (size_t)used < sizeof(input));
	expect_rejected(input, 4);

	input[0] = '\0';
	for (i = 0; i < VR_WEAPON_SCHEMA_MAX_ENTRIES; i++)
	{
		used = snprintf(input + strlen(input), sizeof(input) - strlen(input),
					"{ bitmask 1 } ");
		assert(used > 0);
	}
	count = 0;
	assert(VR_WeaponSchemaParse(input, entries, VR_WEAPON_SCHEMA_MAX_ENTRIES, &count));
	assert(count == VR_WEAPON_SCHEMA_MAX_ENTRIES);
	used = snprintf(input + strlen(input), sizeof(input) - strlen(input), "{ bitmask 1 }");
	assert(used > 0);
	expect_rejected(input, VR_WEAPON_SCHEMA_MAX_ENTRIES);

	count = 55;
	assert(!VR_WeaponSchemaParse("{ bitmask 1 }", entries, 0, &count));
	assert(count == 0);
}

int main(void)
{
	test_installed_id1_schema();
	test_qbj3_ad_schema();
	test_aliases_and_stat_names();
	test_multiplayer_only_offsets_do_not_create_weapon();
	test_rejections_and_bounds();
	puts("weapon schema fixture passed");
	return 0;
}
