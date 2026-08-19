/* Steam library discovery.  This deliberately does not load the Steam API. */
#ifndef QUAKE_STEAM_H
#define QUAKE_STEAM_H

#define QUAKE_STEAM_APPID 2310

typedef struct steam_quake_install_s
{
	char library[MAX_OSPATH];
	char path[MAX_OSPATH];
} steam_quake_install_t;

/* Resolve the appmanifest-selected install directory for Steam Quake. */
qboolean Steam_FindQuakeInstall (steam_quake_install_t *install);

#endif
