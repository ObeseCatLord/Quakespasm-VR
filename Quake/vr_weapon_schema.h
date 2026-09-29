#ifndef VR_WEAPON_SCHEMA_H
#define VR_WEAPON_SCHEMA_H

#include "quakedef.h"

#define VR_WEAPON_SCHEMA_MAX_ENTRIES 64

/* Gesture sensing only: native QC still owns attack range, timing and damage. */
typedef struct
{
	qboolean enabled, has_enabled;
	vec3_t base, tip;
	qboolean has_base, has_tip;
	float speed; // metres/second
	qboolean has_speed;
	int ready_frame;
	qboolean has_ready_frame;
} vr_melee_gesture_profile_t;

/* Authored wheel declarations remain distinct from finished calibration
 * aliases/defaults. Adapted from primary vr.c's VR_SCHEMA field provenance. */
enum
{
	VR_SCHEMA_WHEEL_BITMASK = 1u << 0,
	VR_SCHEMA_WHEEL_IMPULSE = 1u << 1,
	VR_SCHEMA_WHEEL_OWNED_STAT = 1u << 2,
	VR_SCHEMA_WHEEL_OWNED_MASK = 1u << 3,
	VR_SCHEMA_WHEEL_ACTIVE_STAT = 1u << 4,
	VR_SCHEMA_WHEEL_ACTIVE_MASK = 1u << 5,
	VR_SCHEMA_WHEEL_AMMO = 1u << 6,
	VR_SCHEMA_WHEEL_AMMO_STAT = 1u << 7,
	VR_SCHEMA_WHEEL_AMMO_MAX = 1u << 8,
	VR_SCHEMA_WHEEL_MODEL = 1u << 9,
	VR_SCHEMA_WHEEL_VIEWMODEL = 1u << 10,
	VR_SCHEMA_WHEEL_SCALE = 1u << 11,
	VR_SCHEMA_WHEEL_OFFSET = 1u << 12
};

typedef struct
{
	unsigned int fields;
	int bitmask, impulse;
	int owned_stat, owned_mask, active_stat, active_mask;
	int ammo_stat, ammo_max;
	char model_path[64], viewmodel_path[64];
	float scale;
	vec3_t offset;
} vr_weapon_schema_wheel_t;

typedef struct
{
	int bitmask;
	char model_path[64];
	char viewmodel_path[64];
	int impulse;
	float scale;
	vec3_t offset;
	qboolean has_offset;
	float held_scale;
	vec3_t held_offset;
	qboolean has_held_scale;
	qboolean has_held_offset;
	vec3_t muzzle_offset;
	qboolean has_muzzle_offset;
	vec3_t muzzle_source_offset;
	qboolean has_muzzle_source_offset;
	qboolean muzzle_source_viewofs;
	qboolean has_muzzle_source_viewofs;
	qboolean spawn_at_self_origin;
	qboolean has_spawn_at_self_origin;
	int owned_stat;
	int owned_mask;
	int active_stat;
	int active_mask;
	int ammo_stat;
	int ammo_max;
	vec3_t enhanced_held_offset;
	qboolean has_enhanced_held_offset;
	vec3_t enhanced_muzzle_offset;
	qboolean has_enhanced_muzzle_offset;
	vr_melee_gesture_profile_t melee;
	/* Inline snapshot: source text is freed before consumers apply entries. */
	vr_weapon_schema_wheel_t wheel;
} vr_weapon_schema_entry_t;

typedef struct
{
	qboolean complete_roster;
} vr_weapon_schema_metadata_t;

/* Metadata and entries publish together only after successful parsing. */
qboolean VR_WeaponSchemaParseWithMetadata(const char *text,
	vr_weapon_schema_entry_t *entries, size_t capacity, size_t *count,
	vr_weapon_schema_metadata_t *metadata);

qboolean VR_WeaponSchemaParse(const char *text,
							  vr_weapon_schema_entry_t *entries,
							  size_t capacity, size_t *count);

#endif
