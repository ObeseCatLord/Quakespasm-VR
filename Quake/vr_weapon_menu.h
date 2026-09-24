#ifndef QUAKE_VR_WEAPON_MENU_H
#define QUAKE_VR_WEAPON_MENU_H

#include "vr_weapon_catalog.h"

struct cb_context_s;

/* Stable catalog identities let the same screen-space picker serve later
 * profile, model, playspace, and co-op catalogs without owning game state. */
typedef enum {
	VR_WEAPON_MENU_WEAPON,
	VR_WEAPON_MENU_ACTION,
	VR_WEAPON_MENU_PLAYER
} vr_weapon_menu_entry_kind_t;

typedef struct {
	int id;
	vr_weapon_menu_entry_kind_t kind;
	vr_weapon_catalog_source_t source;
	const char *label;
	const char *model_path;
	float model_scale;
	float model_offset[3];
	int selector;
	int impulse;
	int owned_stat;
	int owned_mask;
	int active_stat;
	int active_mask;
	int ammo_stat;
	int ammo_max;
	int ammo_max_stat;
	int has_schema_peer;
	int has_profile_peer;
} vr_weapon_menu_entry_t;

typedef struct {
	const vr_weapon_menu_entry_t *entries;
	size_t count;
	int authoritative_schema;
} vr_weapon_menu_catalog_t;

qboolean VR_WeaponMenu_CanOpen (void);
qboolean VR_WeaponMenu_IsOpen (void);
qboolean VR_WeaponMenu_IsOpenVR (void);
unsigned int VR_WeaponMenu_SessionGeneration (void);
void VR_WeaponMenu_Open (void);
void VR_WeaponMenu_Cancel (void);
/* Reload the optional active-game wwheel.txt catalog at game transitions. */
void VR_WeaponMenu_ReloadGame (void);
void VR_WeaponMenu_SetVRPointer (qboolean tracking_valid, qboolean pointer_valid,
	int pointer_x, int pointer_y);
/* Resolve optional wheel models on the main thread before draw tasks start. */
void VR_WeaponMenu_PrepareModels (void);
void VR_WeaponMenu_SetVRPanel (const float world_from_ndc[16], qboolean playspace);
int VR_WeaponMenu_DrawModels (struct cb_context_s *cbx);
int VR_WeaponMenu_Release (void);
void VR_WeaponMenu_Draw (struct cb_context_s *cbx);

/* Catalog adapters can reuse the same hover and release policy. Stats and
 * item bits are borrowed snapshots; the menu never stores inventory state. */
void VR_WeaponMenu_DrawCatalog (struct cb_context_s *cbx,
	const vr_weapon_menu_catalog_t *catalog, const int *stats, size_t num_stats,
	int client_items);
int VR_WeaponMenu_ReleaseCatalog (const vr_weapon_menu_catalog_t *catalog,
	const int *stats, size_t num_stats, int client_items);

#endif
