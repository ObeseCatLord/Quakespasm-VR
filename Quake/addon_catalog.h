/*
 * Explicit, asynchronous add-on catalogue. Main-thread callers own user
 * consent and connection flow; this module never changes gamedirs or starts
 * game-network connections itself.
 */
#ifndef _ADDON_CATALOG_H_
#define _ADDON_CATALOG_H_

#define ADDON_CATALOG_MAX_ENTRIES 128

typedef enum addon_catalog_state_e
{
	ADDON_CATALOG_IDLE,
	ADDON_CATALOG_REFRESHING,
	ADDON_CATALOG_READY,
	ADDON_CATALOG_INSTALLING,
	ADDON_CATALOG_ERROR,
	ADDON_CATALOG_UNAVAILABLE
} addon_catalog_state_t;

typedef struct addon_catalog_entry_s
{
	char	gamedir[32];
	char	name[64];
	char	author[64];
	char	description[160];
	char	download[160];
	int		size;
	qboolean	installed;
	/* Existing Ironwail manifest entries are transport-authenticated only. */
	qboolean	verified;
} addon_catalog_entry_t;

void AddonCatalog_Init (void);
void AddonCatalog_Shutdown (void);
void AddonCatalog_Poll (void);
void AddonCatalog_Refresh (void);
void AddonCatalog_Cancel (void);
addon_catalog_state_t AddonCatalog_State (void);
const char *AddonCatalog_Message (void);
int AddonCatalog_Count (void);
const addon_catalog_entry_t *AddonCatalog_Entry (int index);
/* Returns the raw catalogue index and copies the matching entry atomically. */
int AddonCatalog_FindGameDir (const char *gamedir,
	addon_catalog_entry_t *entry);
qboolean AddonCatalog_StartInstall (int index, qboolean allow_unverified);
float AddonCatalog_Progress (void);

#endif /* _ADDON_CATALOG_H_ */
