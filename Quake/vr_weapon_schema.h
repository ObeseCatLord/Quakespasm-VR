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
} vr_melee_gesture_profile_t;

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
} vr_weapon_schema_entry_t;

qboolean VR_WeaponSchemaParse(const char *text,
							  vr_weapon_schema_entry_t *entries,
							  size_t capacity, size_t *count);

#endif
