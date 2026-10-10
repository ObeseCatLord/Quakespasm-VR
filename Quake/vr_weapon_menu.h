#ifndef QUAKE_VR_WEAPON_MENU_H
#define QUAKE_VR_WEAPON_MENU_H

#include "vr_weapon_catalog.h"

struct cb_context_s;

extern cvar_t vr_weaponmenu_player_teleport;

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
	/* Held identity is independent of the pickup/preview mesh. */
	const char *viewmodel_path;
	unsigned int schema_fields; /* successful authored keys, not inferred values */
	int game_profile; /* native profile identity survives partial file overlays */
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
void VR_WeaponMenu_List_f (void);
/* Runtime weapon discovery is called on the main thread before draw tasks.
 * Client reset drops borrowed precache/frame pointers while keeping copied
 * learned paths and discovered catalog rows until the next game reload. */
void VR_WeaponMenu_ObserveActive (void);
void VR_WeaponMenu_ClientReset (void);
void VR_WeaponMenu_SetDesktopPointer (qboolean pointer_valid,
	int pointer_x, int pointer_y);
/* SDL publishes the currently selected logical stick before button release
 * commands are queued. The desktop wheel keeps source ownership itself. */
void VR_WeaponMenu_SetDesktopStick (float x, float y, float deadzone);
void VR_WeaponMenu_DesktopMouseMotion (int pointer_x, int pointer_y);
void VR_WeaponMenu_SetVRPointer (qboolean tracking_valid, qboolean pointer_valid,
	int pointer_x, int pointer_y, const float world_from_ndc[16],
	const float ray_origin[3], const float ray_direction[3], qboolean playspace);
/* Resolve optional wheel models on the main thread before draw tasks start. */
void VR_WeaponMenu_PrepareModels (void);
/* Number of occupied rings in the current visible VR catalog, including the
 * center slot's ring. */
int VR_WeaponMenu_VisibleRingCount (void);
void VR_WeaponMenu_SetVRPanel (const float world_from_ndc[16], qboolean playspace);
qboolean VR_WeaponMenu_UsesForegroundDepth (void);
/* Validated co-op action hovered in the current VR wheel, or -1. */
int VR_WeaponMenu_HoveredCoopPlayer (void);
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
