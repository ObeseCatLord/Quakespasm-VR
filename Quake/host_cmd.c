/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include <errno.h>
#include "q_ctype.h"
#ifndef _WIN32
#include <dirent.h>
#endif
#include "vr.h"
#include "debug_log.h"

extern cvar_t pausable;

int current_skill;

/*
==================
Host_Quit_f
==================
*/
void Host_Quit_f(void) {
  if (key_dest != key_console && cls.state != ca_dedicated) {
    M_Menu_Quit_f();
    return;
  }
  CL_Disconnect();
  Host_ShutdownServer(false);

  Sys_Quit();
}

//==============================================================================
// johnfitz -- extramaps management
//==============================================================================

/*
==================
FileList_Add
==================
*/
static filelist_item_t *FileList_Add(const char *name, filelist_item_t **list,
                                    unsigned int path_id) {
  filelist_item_t *item, *cursor, *prev;

  // ignore duplicate
  for (item = *list; item; item = item->next) {
    if (!Q_strcmp(name, item->name))
      return item;
  }

  item = (filelist_item_t *)Z_Malloc(sizeof(filelist_item_t));
  q_strlcpy(item->name, name, sizeof(item->name));
  item->path_id = path_id;

  // insert each entry in alphabetical order
  if (*list == NULL ||
      q_strcasecmp(item->name, (*list)->name) < 0) // insert at front
  {
    item->next = *list;
    *list = item;
  } else // insert later
  {
    prev = *list;
    cursor = (*list)->next;
    while (cursor && (q_strcasecmp(item->name, cursor->name) > 0)) {
      prev = cursor;
      cursor = cursor->next;
    }
    item->next = prev->next;
    prev->next = item;
  }

  return item;
}

static void FileList_Clear(filelist_item_t **list) {
  filelist_item_t *blah;

  while (*list) {
    blah = (*list)->next;
    Z_Free(*list);
    *list = blah;
  }
}

static const filelist_item_t *Modlist_Find(const char *gamedir) {
  filelist_item_t *item;
  if (!gamedir)
    return NULL;
  for (item = modlist; item; item = item->next) {
    if (!q_strcasecmp(item->name, gamedir))
      return item;
  }
  return NULL;
}

static void Modlist_LoadMetadata(filelist_item_t *item) {
  char path[MAX_OSPATH];
  const char *bases[6], *roots[4];
  size_t base_count;
  size_t i;
  int root_count;
  int parsed;

  if (!item || (item->full_name[0] || item->description[0]))
    return;

  base_count = 0;
  if (COM_HasSeparateUserDir())
    bases[base_count++] = host_parms->userdir;
  root_count = COM_GetContentRoots(roots, countof(roots));
  /* Metadata follows the same precedence as mounted content. */
  for (i = root_count; i > 0; --i)
    bases[base_count++] = roots[i - 1];
  bases[base_count++] = com_basedir;

  for (i = 0; i < base_count; i++) {
    char *buf, *cursor, *line, *line_end;
    qboolean has_newline;
    const char *base;
    base = bases[i];
    if (q_snprintf(path, sizeof(path), "%s/%s/descript.ion", base,
                   item->name) >= sizeof(path))
      continue;

    buf = (char *)COM_LoadMallocFile_TextMode_OSPath(path, NULL);
    if (!buf)
      continue;

    parsed = 0;
    cursor = buf;
    while (parsed < 2 && *cursor) {
      while (*cursor == '\r' || *cursor == '\n')
        ++cursor;

      if (!*cursor)
        break;

      line_end = strchr(cursor, '\n');
      has_newline = false;
      if (line_end) {
        *line_end = '\0';
        has_newline = true;
      } else {
        line_end = cursor + strlen(cursor);
      }

      line = cursor;
      while (*line && q_isspace(*line))
        ++line;

      if (*line) {
        while (line_end > line && q_isspace(line_end[-1]))
          --line_end;
        *line_end = '\0';
      }

      if (*line) {
        if (parsed == 0)
          q_strlcpy(item->full_name, line, sizeof(item->full_name));
        else if (parsed == 1)
          q_strlcpy(item->description, line, sizeof(item->description));
        ++parsed;
      }

      if (!has_newline)
        break;
      cursor = line_end + 1;
      if (!*cursor)
        break;
    }
    free(buf);
    if (item->full_name[0] || item->description[0])
      break;
  }
}

const char *Modlist_GetFullName(const filelist_item_t *item) {
  if (!item)
    return "";
  if (item->full_name[0])
    return item->full_name;
  return item->name;
}

const char *Modlist_GetDescription(const filelist_item_t *item) {
  if (!item)
    return "";
  return item->description;
}

qboolean Modlist_GetMetadata(const char *gamedir, const char **name,
                             const char **description) {
  const filelist_item_t *item;
  if (!gamedir || (!name && !description))
    return false;

  item = Modlist_Find(gamedir);
  if (!item)
    return false;

  if (name)
    *name = item->full_name[0] ? item->full_name : NULL;
  if (description)
    *description = item->description[0] ? item->description : NULL;
  return true;
}

filelist_item_t *extralevels;

static void ExtraMaps_Add(const char *name, unsigned int path_id) {
  FileList_Add(name, &extralevels, path_id);
}

void ExtraMaps_Init(void) {
#ifdef _WIN32
  WIN32_FIND_DATA fdat;
  HANDLE fhnd;
#else
  DIR *dir_p;
  struct dirent *dir_t;
#endif
  char filestring[MAX_OSPATH];
  char mapname[32];
  searchpath_t *search;
  pack_t *pak;
  int i;

  for (search = com_searchpaths; search; search = search->next) {
    if (*search->filename) // directory
    {
#ifdef _WIN32
      q_snprintf(filestring, sizeof(filestring), "%s/maps/*.bsp",
                 search->filename);
      fhnd = FindFirstFile(filestring, &fdat);
      if (fhnd == INVALID_HANDLE_VALUE)
        continue;
      do {
        COM_StripExtension(fdat.cFileName, mapname, sizeof(mapname));
        ExtraMaps_Add(mapname, search->path_id);
      } while (FindNextFile(fhnd, &fdat));
      FindClose(fhnd);
#else
      q_snprintf(filestring, sizeof(filestring), "%s/maps/", search->filename);
      dir_p = opendir(filestring);
      if (dir_p == NULL)
        continue;
      while ((dir_t = readdir(dir_p)) != NULL) {
        if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "bsp") != 0)
          continue;
        COM_StripExtension(dir_t->d_name, mapname, sizeof(mapname));
        ExtraMaps_Add(mapname, search->path_id);
      }
      closedir(dir_p);
#endif
    } else // pakfile
    {
      if (search->path_id != 1U) { // don't list standard id maps
        for (i = 0, pak = search->pack; i < pak->numfiles; i++) {
          if (!strcmp(COM_FileGetExtension(pak->files[i].name), "bsp")) {
            if (pak->files[i].filelen >
                32 * 1024) { // don't list files under 32k (ammo boxes etc)
              COM_StripExtension(pak->files[i].name + 5, mapname,
                                 sizeof(mapname));
              ExtraMaps_Add(mapname, search->path_id);
            }
          }
        }
      }
    }
  }
}

static void ExtraMaps_Clear(void) { FileList_Clear(&extralevels); }

void ExtraMaps_NewGame(void) {
  ExtraMaps_Clear();
  ExtraMaps_Init();
}

/*
==================
Host_Maps_f
==================
*/
static void ExtraMaps_List(const char *mod_name) {
  int i;
  unsigned int path_id = 0;
  filelist_item_t *level;

  if (mod_name) {
    searchpath_t *search;

    Con_SafePrintf("maps in search path from directory \"%s\":\n", mod_name);
    for (search = com_searchpaths; search; search = search->next) {
      if (!search->pack &&
          !q_strcasecmp(COM_SkipPath(search->filename), mod_name)) {
        path_id = search->path_id;
        break;
      }
    }
    if (!path_id) {
      Con_SafePrintf("game %s not loaded\n", mod_name);
      return;
    }
  }

  for (level = extralevels, i = 0; level; level = level->next) {
    if (path_id && path_id != level->path_id)
      continue;
    i++;
    Con_SafePrintf("   %s\n", level->name);
  }

  if (i)
    Con_SafePrintf("%i map(s)\n", i);
  else
    Con_SafePrintf("no maps found\n");
}

/*
==================
Host_Maps_f
==================
*/
static void Host_Maps_f(void) {
  ExtraMaps_List(Cmd_Argc() > 1 ? Cmd_Argv(1) : NULL);
}

static void Host_Maps_Mod_f(void) {
  ExtraMaps_List(COM_SkipPath(com_gamedir));
}

//==============================================================================
// johnfitz -- modlist management
//==============================================================================

filelist_item_t *modlist;

static void Modlist_Add(const char *name) {
  filelist_item_t *item;
  item = FileList_Add(name, &modlist, 0);
  if (item) {
    Modlist_LoadMetadata(item);
  }
}

static qboolean Modlist_ValidName(const char *name) {
  const unsigned char *c;

  if (!*name || !strcmp(name, ".") || !strcmp(name, ".."))
    return false;

  for (c = (const unsigned char *)name; *c; c++) {
    if (*c < ' ' || *c == '"' || *c == ';' || *c == '/' || *c == '\\' ||
        *c == ':')
      return false;
  }

  return true;
}

/*
==================
Modlist_HasContents

Empty directories and unrelated build/install folders should not appear in the
Mods menu.  A Quake game directory normally carries either a pak/progs file or
at least one of the usual asset directories with content.
==================
*/
static qboolean Modlist_HasContents(const char *path) {
#ifdef _WIN32
  WIN32_FIND_DATA fdat;
  HANDLE fhnd;
  char findpath[MAX_OSPATH];

  q_snprintf(findpath, sizeof(findpath), "%s/*", path);
  fhnd = FindFirstFile(findpath, &fdat);
  if (fhnd == INVALID_HANDLE_VALUE)
    return false;

  do {
    if (strcmp(fdat.cFileName, ".") && strcmp(fdat.cFileName, "..")) {
      FindClose(fhnd);
      return true;
    }
  } while (FindNextFile(fhnd, &fdat));

  FindClose(fhnd);
  return false;
#else
  DIR *dir_p;
  struct dirent *dir_t;

  dir_p = opendir(path);
  if (dir_p == NULL)
    return false;

  while ((dir_t = readdir(dir_p)) != NULL) {
    if (strcmp(dir_t->d_name, ".") && strcmp(dir_t->d_name, "..")) {
      closedir(dir_p);
      return true;
    }
  }

  closedir(dir_p);
  return false;
#endif
}

static qboolean Modlist_Check(const char *base, const char *name) {
  static const char *const assetdirs[] = {"maps", "progs", "gfx", "sound"};
  char modpath[MAX_OSPATH];
  char itempath[MAX_OSPATH];
  size_t i;

  if (!Modlist_ValidName(name))
    return false;

  q_snprintf(modpath, sizeof(modpath), "%s/%s", base, name);

  if (COM_DirectoryHasPak0(modpath))
    return true;

  q_snprintf(itempath, sizeof(itempath), "%s/progs.dat", modpath);
  if (Sys_FileType(itempath) & FS_ENT_FILE)
    return true;

  q_snprintf(itempath, sizeof(itempath), "%s/csprogs.dat", modpath);
  if (Sys_FileType(itempath) & FS_ENT_FILE)
    return true;

  for (i = 0; i < countof(assetdirs); i++) {
    q_snprintf(itempath, sizeof(itempath), "%s/%s", modpath, assetdirs[i]);
    if ((Sys_FileType(itempath) & FS_ENT_DIRECTORY) &&
        Modlist_HasContents(itempath))
      return true;
  }

  return false;
}

#ifdef _WIN32
static void Modlist_ScanRoot(const char *root) {
  WIN32_FIND_DATA fdat;
  HANDLE fhnd;
  DWORD attribs;
  char dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

  q_snprintf(dir_string, sizeof(dir_string), "%s/*", root);
  fhnd = FindFirstFile(dir_string, &fdat);
  if (fhnd == INVALID_HANDLE_VALUE)
    return;

  do {
    if (!strcmp(fdat.cFileName, ".") || !strcmp(fdat.cFileName, ".."))
      continue;
    q_snprintf(mod_string, sizeof(mod_string), "%s/%s", root,
               fdat.cFileName);
    attribs = GetFileAttributes(mod_string);
    if (attribs != INVALID_FILE_ATTRIBUTES &&
        (attribs & FILE_ATTRIBUTE_DIRECTORY) &&
        Modlist_Check(root, fdat.cFileName)) {
      Modlist_Add(fdat.cFileName);
    }
  } while (FindNextFile(fhnd, &fdat));

  FindClose(fhnd);
}
#else
static void Modlist_ScanRoot(const char *root) {
  DIR *dir_p, *mod_dir_p;
  struct dirent *dir_t;
  char dir_string[MAX_OSPATH], mod_string[MAX_OSPATH];

  q_snprintf(dir_string, sizeof(dir_string), "%s/", root);
  dir_p = opendir(dir_string);
  if (dir_p == NULL)
    return;

  while ((dir_t = readdir(dir_p)) != NULL) {
    if (!strcmp(dir_t->d_name, ".") || !strcmp(dir_t->d_name, ".."))
      continue;
    if (!q_strcasecmp(COM_FileGetExtension(dir_t->d_name),
                      "app")) // skip .app bundles on macOS
      continue;
    q_snprintf(mod_string, sizeof(mod_string), "%s%s/", dir_string,
               dir_t->d_name);
    mod_dir_p = opendir(mod_string);
    if (mod_dir_p == NULL)
      continue;
    closedir(mod_dir_p);
    if (Modlist_Check(root, dir_t->d_name))
      Modlist_Add(dir_t->d_name);
  }

  closedir(dir_p);
}
#endif

void Modlist_Init(void) {
	const char *roots[4];
	int i, root_count;
  Modlist_ScanRoot(com_basedir);
	root_count = COM_GetContentRoots(roots, countof(roots));
	for (i = 0; i < root_count; ++i)
		Modlist_ScanRoot(roots[i]);
  if (COM_HasSeparateUserDir())
    Modlist_ScanRoot(host_parms->userdir);
}

void Modlist_Rebuild(void) {
  FileList_Clear(&modlist);
  Modlist_Init();
}

//==============================================================================
// ericw -- demo list management
//==============================================================================

filelist_item_t *demolist;

static void DemoList_Clear(void) { FileList_Clear(&demolist); }

void DemoList_Rebuild(void) {
  DemoList_Clear();
  DemoList_Init();
}

// TODO: Factor out to a general-purpose file searching function
void DemoList_Init(void) {
#ifdef _WIN32
  WIN32_FIND_DATA fdat;
  HANDLE fhnd;
#else
  DIR *dir_p;
  struct dirent *dir_t;
#endif
  char filestring[MAX_OSPATH];
  char demname[32];
  searchpath_t *search;
  pack_t *pak;
  int i;

  for (search = com_searchpaths; search; search = search->next) {
    if (*search->filename) // directory
    {
#ifdef _WIN32
      q_snprintf(filestring, sizeof(filestring), "%s/*.dem", search->filename);
      fhnd = FindFirstFile(filestring, &fdat);
      if (fhnd == INVALID_HANDLE_VALUE)
        continue;
      do {
        COM_StripExtension(fdat.cFileName, demname, sizeof(demname));
        FileList_Add(demname, &demolist, search->path_id);
      } while (FindNextFile(fhnd, &fdat));
      FindClose(fhnd);
#else
      q_snprintf(filestring, sizeof(filestring), "%s/", search->filename);
      dir_p = opendir(filestring);
      if (dir_p == NULL)
        continue;
      while ((dir_t = readdir(dir_p)) != NULL) {
        if (q_strcasecmp(COM_FileGetExtension(dir_t->d_name), "dem") != 0)
          continue;
        COM_StripExtension(dir_t->d_name, demname, sizeof(demname));
        FileList_Add(demname, &demolist, search->path_id);
      }
      closedir(dir_p);
#endif
    } else // pakfile
    {
      if (search->path_id != 1U) { // don't list standard id demos
        for (i = 0, pak = search->pack; i < pak->numfiles; i++) {
          if (!strcmp(COM_FileGetExtension(pak->files[i].name), "dem")) {
            COM_StripExtension(pak->files[i].name, demname, sizeof(demname));
            FileList_Add(demname, &demolist, search->path_id);
          }
        }
      }
    }
  }
}

/*
==================
Host_Mods_f -- johnfitz

list all potential mod directories (contain either a pak file or a progs.dat)
==================
*/
static void Host_Mods_f(void) {
  int i;
  filelist_item_t *mod;

  for (mod = modlist, i = 0; mod; mod = mod->next, i++)
    Con_SafePrintf("   %s\n", mod->name);

  if (i)
    Con_SafePrintf("%i mod(s)\n", i);
  else
    Con_SafePrintf("no mods found\n");
}

//==============================================================================

/*
=============
Host_Mapname_f -- johnfitz
=============
*/
static void Host_Mapname_f(void) {
  if (sv.active) {
    Con_Printf("\"mapname\" is \"%s\"\n", sv.name);
    return;
  }

  if (cls.state == ca_connected) {
    Con_Printf("\"mapname\" is \"%s\"\n", cl.mapname);
    return;
  }

  Con_Printf("no map loaded\n");
}

/*
==================
Host_Status_f
==================
*/
static void Host_Status_f(void) {
  void (*print_fn)(const char *fmt, ...) FUNCP_PRINTF(1, 2);
  client_t *client;
  int seconds;
  int minutes;
  int hours = 0;
  int j;

  if (cmd_source == src_command) {
    if (!sv.active) {
      Cmd_ForwardToServer();
      return;
    }
    print_fn = Con_Printf;
  } else
    print_fn = SV_ClientPrintf;

  print_fn("host:    %s\n", Cvar_VariableString("hostname"));
  print_fn("version: %4.2f\n", VERSION);
  if (tcpipAvailable)
    print_fn("tcp/ip:  %s\n", my_tcpip_address);
  if (ipxAvailable)
    print_fn("ipx:     %s\n", my_ipx_address);
  print_fn("map:     %s\n", sv.name);
  print_fn("players: %i active (%i max)\n\n", net_activeconnections,
           svs.maxclients);
  for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
    if (!client->active)
      continue;
    seconds = (int)(net_time - NET_QSocketGetTime(client->netconnection));
    minutes = seconds / 60;
    if (minutes) {
      seconds -= (minutes * 60);
      hours = minutes / 60;
      if (hours)
        minutes -= (hours * 60);
    } else
      hours = 0;
    print_fn("#%-2u %-16.16s  %3i  %2i:%02i:%02i\n", j + 1, client->name,
             (int)client->edict->v.frags, hours, minutes, seconds);
    print_fn("   %s\n", NET_QSocketGetAddressString(client->netconnection));
  }
}

/*
==================
Host_God_f

Sets client to godmode
==================
*/
static void Host_God_f(void) {
  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (pr_global_struct->deathmatch)
    return;

  // johnfitz -- allow user to explicitly set god mode to on or off
  switch (Cmd_Argc()) {
  case 1:
    sv_player->v.flags = (int)sv_player->v.flags ^ FL_GODMODE;
    if (!((int)sv_player->v.flags & FL_GODMODE))
      SV_ClientPrintf("godmode OFF\n");
    else
      SV_ClientPrintf("godmode ON\n");
    break;
  case 2:
    if (Q_atof(Cmd_Argv(1))) {
      sv_player->v.flags = (int)sv_player->v.flags | FL_GODMODE;
      SV_ClientPrintf("godmode ON\n");
    } else {
      sv_player->v.flags = (int)sv_player->v.flags & ~FL_GODMODE;
      SV_ClientPrintf("godmode OFF\n");
    }
    break;
  default:
    Con_Printf("god [value] : toggle god mode. values: 0 = off, 1 = on\n");
    break;
  }
  // johnfitz
}

/*
==================
Host_Notarget_f
==================
*/
static void Host_Notarget_f(void) {
  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (pr_global_struct->deathmatch)
    return;

  // johnfitz -- allow user to explicitly set notarget to on or off
  switch (Cmd_Argc()) {
  case 1:
    sv_player->v.flags = (int)sv_player->v.flags ^ FL_NOTARGET;
    if (!((int)sv_player->v.flags & FL_NOTARGET))
      SV_ClientPrintf("notarget OFF\n");
    else
      SV_ClientPrintf("notarget ON\n");
    break;
  case 2:
    if (Q_atof(Cmd_Argv(1))) {
      sv_player->v.flags = (int)sv_player->v.flags | FL_NOTARGET;
      SV_ClientPrintf("notarget ON\n");
    } else {
      sv_player->v.flags = (int)sv_player->v.flags & ~FL_NOTARGET;
      SV_ClientPrintf("notarget OFF\n");
    }
    break;
  default:
    Con_Printf(
        "notarget [value] : toggle notarget mode. values: 0 = off, 1 = on\n");
    break;
  }
  // johnfitz
}

qboolean noclip_anglehack;

/*
==================
Host_Noclip_f
==================
*/
static void Host_Noclip_f(void) {
  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (pr_global_struct->deathmatch)
    return;

  // johnfitz -- allow user to explicitly set noclip to on or off
  switch (Cmd_Argc()) {
  case 1:
    if (sv_player->v.movetype != MOVETYPE_NOCLIP) {
      noclip_anglehack = true;
      sv_player->v.movetype = MOVETYPE_NOCLIP;
      SV_ClientPrintf("noclip ON\n");
    } else {
      noclip_anglehack = false;
      sv_player->v.movetype = MOVETYPE_WALK;
      SV_ClientPrintf("noclip OFF\n");
    }
    break;
  case 2:
    if (Q_atof(Cmd_Argv(1))) {
      noclip_anglehack = true;
      sv_player->v.movetype = MOVETYPE_NOCLIP;
      SV_ClientPrintf("noclip ON\n");
    } else {
      noclip_anglehack = false;
      sv_player->v.movetype = MOVETYPE_WALK;
      SV_ClientPrintf("noclip OFF\n");
    }
    break;
  default:
    Con_Printf(
        "noclip [value] : toggle noclip mode. values: 0 = off, 1 = on\n");
    break;
  }
  // johnfitz
}

/*
====================
Host_SetPos_f

adapted from fteqw, originally by Alex Shadowalker
====================
*/
static void Host_SetPos_f(void) {
  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (pr_global_struct->deathmatch)
    return;

  if (Cmd_Argc() != 7 && Cmd_Argc() != 4) {
    SV_ClientPrintf("usage:\n");
    SV_ClientPrintf("   setpos <x> <y> <z>\n");
    SV_ClientPrintf("   setpos <x> <y> <z> <pitch> <yaw> <roll>\n");
    SV_ClientPrintf("current values:\n");
    SV_ClientPrintf("   %i %i %i %i %i %i\n", (int)sv_player->v.origin[0],
                    (int)sv_player->v.origin[1], (int)sv_player->v.origin[2],
                    (int)sv_player->v.v_angle[0], (int)sv_player->v.v_angle[1],
                    (int)sv_player->v.v_angle[2]);
    return;
  }

  if (sv_player->v.movetype != MOVETYPE_NOCLIP) {
    noclip_anglehack = true;
    sv_player->v.movetype = MOVETYPE_NOCLIP;
    SV_ClientPrintf("noclip ON\n");
  }

  // make sure they're not going to whizz away from it
  sv_player->v.velocity[0] = 0;
  sv_player->v.velocity[1] = 0;
  sv_player->v.velocity[2] = 0;

  sv_player->v.origin[0] = atof(Cmd_Argv(1));
  sv_player->v.origin[1] = atof(Cmd_Argv(2));
  sv_player->v.origin[2] = atof(Cmd_Argv(3));

  if (Cmd_Argc() == 7) {
    sv_player->v.angles[0] = atof(Cmd_Argv(4));
    sv_player->v.angles[1] = atof(Cmd_Argv(5));
    sv_player->v.angles[2] = atof(Cmd_Argv(6));
    sv_player->v.fixangle = 1;
  }

  SV_LinkEdict(sv_player, false);
}

/*
==================
Host_Fly_f

Sets client to flymode
==================
*/
static void Host_Fly_f(void) {
  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (pr_global_struct->deathmatch)
    return;

  // johnfitz -- allow user to explicitly set noclip to on or off
  switch (Cmd_Argc()) {
  case 1:
    if (sv_player->v.movetype != MOVETYPE_FLY) {
      sv_player->v.movetype = MOVETYPE_FLY;
      SV_ClientPrintf("flymode ON\n");
    } else {
      sv_player->v.movetype = MOVETYPE_WALK;
      SV_ClientPrintf("flymode OFF\n");
    }
    break;
  case 2:
    if (Q_atof(Cmd_Argv(1))) {
      sv_player->v.movetype = MOVETYPE_FLY;
      SV_ClientPrintf("flymode ON\n");
    } else {
      sv_player->v.movetype = MOVETYPE_WALK;
      SV_ClientPrintf("flymode OFF\n");
    }
    break;
  default:
    Con_Printf("fly [value] : toggle fly mode. values: 0 = off, 1 = on\n");
    break;
  }
  // johnfitz
}

/*
==================
Host_Ping_f

==================
*/
static void Host_Ping_f(void) {
  int i, j;
  float total;
  client_t *client;

  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  SV_ClientPrintf("Client ping times:\n");
  for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++) {
    if (!client->active)
      continue;
    total = 0;
    for (j = 0; j < NUM_PING_TIMES; j++)
      total += client->ping_times[j];
    total /= NUM_PING_TIMES;
    SV_ClientPrintf("%4i %s\n", (int)(total * 1000), client->name);
  }
}

/*
===============================================================================

SERVER TRANSITIONS

===============================================================================
*/

/*
======================
Host_Map_f

handle a
map <servername>
command from the console.  Active clients are kicked off.
======================
*/
static void Host_Map_f(void) {
  int i;
  char name[MAX_QPATH], *p;

  if (Cmd_Argc() < 2) // no map name given
  {
    if (cls.state == ca_dedicated) {
      if (sv.active)
        Con_Printf("Current map: %s\n", sv.name);
      else
        Con_Printf("Server not active\n");
    } else if (cls.state == ca_connected) {
      Con_Printf("Current map: %s ( %s )\n", cl.levelname, cl.mapname);
    } else {
      Con_Printf("map <levelname>: start a new server\n");
    }
    return;
  }

  if (cmd_source != src_command)
    return;

  cls.demonum = -1; // stop demo loop in case this fails

  CL_Disconnect();
  Host_ShutdownServer(false);

  if (cls.state != ca_dedicated)
    IN_Activate();
  key_dest = key_game; // remove console or menu
  SCR_BeginLoadingPlaque();

  svs.serverflags = 0; // haven't completed an episode yet
  svs.coop_loadgame_late_join_spawns_near = false;
  SV_MG3UpgradeResetCampaign();
  q_strlcpy(name, Cmd_Argv(1), sizeof(name));
  // remove a final ".bsp" extension from mapname -- S.A.
  p = strrchr(name, '.');
  if (p && !strcmp(p, ".bsp"))
    *p = '\0';
  PR_SwitchQCVM(&sv.qcvm);
  SV_SpawnServer(name);
  PR_SwitchQCVM(NULL);
  if (!sv.active)
    return;

  if (cls.state != ca_dedicated) {
    memset(cls.spawnparms, 0, MAX_MAPSTRING);
    for (i = 2; i < Cmd_Argc(); i++) {
      q_strlcat(cls.spawnparms, Cmd_Argv(i), MAX_MAPSTRING);
      q_strlcat(cls.spawnparms, " ", MAX_MAPSTRING);
    }

    Cmd_ExecuteString("connect local", src_command);
  }
}

/*
======================
Host_Randmap_f

Loads a random map from the "maps" list.
======================
*/
static void Host_Randmap_f(void) {
  int i, randlevel, numlevels;
  filelist_item_t *level;

  if (cmd_source != src_command)
    return;

  for (level = extralevels, numlevels = 0; level; level = level->next)
    numlevels++;

  if (numlevels == 0) {
    Con_Printf("no maps\n");
    return;
  }

  randlevel = (rand() % numlevels);

  for (level = extralevels, i = 0; level; level = level->next, i++) {
    if (i == randlevel) {
      Con_Printf("Starting map %s...\n", level->name);
      Cbuf_AddText(va("map %s\n", level->name));
      return;
    }
  }
}

/*
==================
Host_Changelevel_f

Goes to a new map, taking all clients along
==================
*/
static void Host_Changelevel_f(void) {
  char level[MAX_QPATH];

  if (Cmd_Argc() != 2) {
    Con_Printf("changelevel <levelname> : continue game on a new level\n");
    return;
  }
  if (!sv.active || cls.demoplayback) {
    Con_Printf("Only the server may changelevel\n");
    return;
  }

  // johnfitz -- check for client having map before anything else
  q_snprintf(level, sizeof(level), "maps/%s.bsp", Cmd_Argv(1));
  if (!COM_FileExists(level, NULL))
    Host_Error("cannot find map %s", level);
  // johnfitz

  if (cls.state != ca_dedicated)
    IN_Activate();     // -- S.A.
  key_dest = key_game; // remove console or menu
  PR_SwitchQCVM(&sv.qcvm);
  SV_SaveSpawnparms();
  q_strlcpy(level, Cmd_Argv(1), sizeof(level));
  SV_SpawnServer(level);
  PR_SwitchQCVM(NULL);
  // also issue an error if spawn failed -- O.S.
  if (!sv.active)
    Host_Error("cannot run map %s", level);
}

/*
==================
Host_Restart_f

Restarts the current server for a dead player
==================
*/
static void Host_Restart_f(void) {
  char mapname[MAX_QPATH];

  if (cls.demoplayback || !sv.active)
    return;

  if (cmd_source != src_command)
    return;
  q_strlcpy(mapname, sv.name,
            sizeof(mapname)); // mapname gets cleared in spawnserver
  PR_SwitchQCVM(&sv.qcvm);
  SV_SpawnServer(mapname);
  PR_SwitchQCVM(NULL);
  if (!sv.active)
    Host_Error("cannot restart map %s", mapname);
}

/*
==================
Host_Reconnect_f

This command causes the client to wait for the signon messages again.
This is sent just before a server changes levels
==================
*/
static void Host_Reconnect_f(void) {
  if (cls.demoplayback) // cross-map demo playback fix from Baker
    return;

  DebugLog("Host_Reconnect_f: signon=%d state=%d\n", cls.signon, cls.state);
  SCR_BeginLoadingPlaque();
  CL_ClearSignons(); // need new connection messages
  DebugLog("Host_Reconnect_f: signons cleared, waiting for new signon data\n");
}

/*
=====================
Host_Connect_f

User command to connect to server
=====================
*/
static void Host_Connect_f(void) {
  char name[MAX_QPATH];

  CL_AutoReconnect_Cancel();
  cls.demonum = -1; // stop demo loop in case this fails
  if (cls.demoplayback) {
    CL_StopPlayback();
    CL_Disconnect();
  }
  q_strlcpy(name, Cmd_Argv(1), sizeof(name));
  CL_EstablishConnection(name);
  Host_Reconnect_f();
}

/*
===============================================================================

LOAD / SAVE GAME

===============================================================================
*/

#define SAVEGAME_LEGACY_VERSION 5
#define SAVEGAME_MULTICLIENT_VERSION 6
#define SAVEGAME_VERSION 7

static void Host_SaveClientSpawnParms(client_t *client);
#define COOP_AUTOSAVE_MAPSTART_DELAY 3.0
#define COOP_AUTOSAVE_MAX_SLOTS 20

/*
===============
Host_SavegameComment

Writes a SAVEGAME_COMMENT_LENGTH character comment describing the current
===============
*/
static void Host_SavegameComment(char text[SAVEGAME_COMMENT_LENGTH + 1]) {
  int i;
  int killed;
  int total;
  char kills[20];
  const char *levelname;
  char *p;

  for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++)
    text[i] = ' ';
  text[SAVEGAME_COMMENT_LENGTH] = '\0';

  levelname = cl.levelname;
  killed = cl.stats[STAT_MONSTERS];
  total = cl.stats[STAT_TOTALMONSTERS];
  if (sv.active && pr_global_struct) {
    if (!levelname[0])
      levelname = sv.name;
    killed = (int)pr_global_struct->killed_monsters;
    total = (int)pr_global_struct->total_monsters;
  }

  i = (int)strlen(levelname);
  if (i > 22)
    i = 22;
  memcpy(text, levelname, (size_t)i);

  // Remove CR/LFs from level name to avoid broken saves, e.g. with autumn_sp
  // map: https://celephais.net/board/view_thread.php?id=60452&start=3666
  while ((p = strchr(text, '\n')) != NULL)
    *p = ' ';
  while ((p = strchr(text, '\r')) != NULL)
    *p = ' ';

  sprintf(kills, "kills:%3i/%3i", killed, total);
  memcpy(text + 22, kills, strlen(kills));

  // convert space to _ to make stdio happy
  for (i = 0; i < SAVEGAME_COMMENT_LENGTH; i++) {
    if (text[i] == ' ')
      text[i] = '_';
  }
}

static int Host_SavegameActiveClients(void) {
  int i;
  int active_clients;

  active_clients = 0;
  for (i = 0; i < svs.maxclients; i++) {
    if (svs.clients[i].active)
      active_clients++;
  }
  return active_clients;
}

static void Host_SavegameWriteClientName(FILE *f, const char *name) {
  int i;

  if (!name)
    name = "";

  for (i = 0; i < MAX_SCOREBOARDNAME - 1 && name[i]; i++)
    fprintf(f, "%02x", (unsigned char)name[i]);
  fprintf(f, "\n");
}

static void Host_LoadgameReadClientName(const char *encoded, char *name,
                                        size_t name_size) {
  size_t out;
  const char *p;

  if (!name || name_size == 0)
    return;

  name[0] = 0;
  if (!encoded)
    return;

  out = 0;
  p = encoded;
  while (p[0] && p[1] && out + 1 < name_size) {
    int hi, lo;

    if (p[0] >= '0' && p[0] <= '9')
      hi = p[0] - '0';
    else if (p[0] >= 'a' && p[0] <= 'f')
      hi = p[0] - 'a' + 10;
    else if (p[0] >= 'A' && p[0] <= 'F')
      hi = p[0] - 'A' + 10;
    else
      break;

    if (p[1] >= '0' && p[1] <= '9')
      lo = p[1] - '0';
    else if (p[1] >= 'a' && p[1] <= 'f')
      lo = p[1] - 'a' + 10;
    else if (p[1] >= 'A' && p[1] <= 'F')
      lo = p[1] - 'A' + 10;
    else
      break;

    name[out++] = (char)((hi << 4) | lo);
    p += 2;
  }
  name[out] = 0;
}

static const char *Host_LoadgameReadLine(const char *data, char *line,
                                         size_t line_size) {
  const char *newline;
  size_t length;

  if (!data || !line || line_size == 0)
    return NULL;
  newline = strchr(data, '\n');
  if (!newline)
    return NULL;
  length = (size_t)(newline - data);
  if (length && data[length - 1] == '\r')
    length--;
  if (length >= line_size)
    return NULL;
  memcpy(line, data, length);
  line[length] = 0;
  return newline + 1;
}

static qboolean Host_LoadgameParseInt(const char **data, int *value) {
  char line[128];
  char *end;
  const char *next;
  long parsed;

  if (!data || !*data)
    return false;
  next = Host_LoadgameReadLine(*data, line, sizeof(line));
  if (!next)
    return false;
  errno = 0;
  parsed = strtol(line, &end, 10);
  while (*end == ' ' || *end == '\t')
    end++;
  if (errno == ERANGE || end == line || *end || parsed < INT_MIN ||
      parsed > INT_MAX)
    return false;
  *value = (int)parsed;
  *data = next;
  return true;
}

static qboolean Host_LoadgameParseFloat(const char **data, float *value) {
  char line[128];
  char *end;
  const char *next;
  float parsed;

  if (!data || !*data)
    return false;
  next = Host_LoadgameReadLine(*data, line, sizeof(line));
  if (!next)
    return false;
  errno = 0;
  parsed = strtof(line, &end);
  while (*end == ' ' || *end == '\t')
    end++;
  if (errno == ERANGE || end == line || *end || !isfinite(parsed))
    return false;
  *value = parsed;
  *data = next;
  return true;
}

static qboolean Host_LoadgameParseString(const char **data) {
  const char *next;

  if (!data || !*data)
    return false;
  next = Host_LoadgameReadLine(*data, com_token, sizeof(com_token));
  if (!next || !com_token[0])
    return false;
  *data = next;
  return true;
}

static void Host_SavegameRefreshClientSpawnParms(void) {
  int i;
  client_t *old_host_client;
  edict_t *old_sv_player;

  old_host_client = host_client;
  old_sv_player = sv_player;
  for (i = 0; i < svs.maxclients; i++) {
    if (!svs.clients[i].active || !svs.clients[i].edict)
      continue;
    host_client = &svs.clients[i];
    sv_player = host_client->edict;
    Host_SaveClientSpawnParms(host_client);
  }
  /* MG3 upgrades are campaign-wide.  The first pass builds their union from
     every client; apply that complete union to every saved client afterward. */
  for (i = 0; i < svs.maxclients; i++) {
    if (!svs.clients[i].active)
      continue;
    SV_MG3UpgradeApplySpawnParms(svs.clients[i].spawn_parms);
  }
  host_client = old_host_client;
  sv_player = old_sv_player;
}

static qboolean Host_SavegameCanSave(qboolean quiet) {
  if (!sv.active) {
    if (!quiet)
      Con_Printf("Not playing a local game.\n");
    return false;
  }

  if (cl.intermission) {
    if (!quiet)
      Con_Printf("Can't save in intermission.\n");
    return false;
  }

  if (svs.maxclients != 1) {
    if (!sv_save_multiplayer.value) {
      if (!quiet)
        Con_Printf("Can't save multiplayer games unless sv_save_multiplayer is 1.\n");
      return false;
    }
    if (!coop.value || deathmatch.value) {
      if (!quiet)
        Con_Printf("Multiplayer saves are only supported for coop games.\n");
      return false;
    }
  }

  if (svs.maxclients == 1) {
    if (svs.clients[0].active && svs.clients[0].edict->v.health <= 0) {
      if (!quiet)
        Con_Printf("Can't savegame with a dead player\n");
      return false;
    }
  } else if (Host_SavegameActiveClients() <= 0) {
    if (!quiet)
      Con_Printf("Can't save a multiplayer game without active players.\n");
    return false;
  }

  return true;
}

static qboolean Host_LoadgameHasPendingClients(void);
static void Host_LoadgameDiscardPendingClients(qboolean quiet);

static qboolean Host_SavegameReplaceFile(const char *tempname,
                                         const char *name) {
#ifdef _WIN32
  return MoveFileExA(tempname, name,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(tempname, name) == 0;
#endif
}

static qboolean Host_SavegameWrite(const char *savename, qboolean quiet) {
  char name[MAX_OSPATH], tempname[MAX_OSPATH];
  FILE *f;
  int i, j;
  int frags;
  char comment[SAVEGAME_COMMENT_LENGTH + 1];
  qboolean switched_qcvm, write_failed;

  if (!Host_SavegameCanSave(quiet))
    return false;

  /* Automatic saves must never invalidate players still pending from a
     multiplayer load.  Explicit saves retain the existing discard policy. */
  if (quiet && Host_LoadgameHasPendingClients())
    return false;

  if (strstr(savename, "..")) {
    if (!quiet)
      Con_Printf("Relative pathnames are not allowed.\n");
    return false;
  }

  q_snprintf(name, sizeof(name), "%s/%s", com_gamedir, savename);
  COM_AddExtension(name, ".sav", sizeof(name));
  if (q_snprintf(tempname, sizeof(tempname), "%s.tmp", name) >=
      (int)sizeof(tempname)) {
    if (!quiet)
      Con_Printf("ERROR: savegame path is too long.\n");
    return false;
  }

  if (!quiet)
    Con_Printf("Saving game to %s...\n", name);
  remove(tempname);
  f = fopen(tempname, "w");
  if (!f) {
    if (!quiet)
      Con_Printf("ERROR: couldn't open.\n");
    else
      Con_DPrintf("Coop autosave: couldn't open %s\n", name);
    return false;
  }

  switched_qcvm = false;
  if (qcvm != &sv.qcvm) {
    PR_SwitchQCVM(&sv.qcvm);
    switched_qcvm = true;
  }

  Host_SavegameRefreshClientSpawnParms();

  fprintf(f, "%i\n", SAVEGAME_VERSION);
  Host_SavegameComment(comment);
  fprintf(f, "%s\n", comment);
  fprintf(f, "%i\n", svs.maxclients);
  for (i = 0; i < svs.maxclients; i++) {
    frags = 0;
    if (svs.clients[i].edict)
      frags = (int)svs.clients[i].edict->v.frags;
    fprintf(f, "%i\n", svs.clients[i].active ? 1 : 0);
    Host_SavegameWriteClientName(f, svs.clients[i].name);
    fprintf(f, "%i\n", svs.clients[i].colors);
    fprintf(f, "%i\n", frags);
    for (j = 0; j < NUM_SPAWN_PARMS; j++)
      fprintf(f, "%f\n", svs.clients[i].spawn_parms[j]);
  }
  fprintf(f, "%d\n", current_skill);
  fprintf(f, "%s\n", sv.name);
  fprintf(f, "%f\n", qcvm->time);

  // write the light styles
  for (i = 0; i < MAX_LIGHTSTYLES; i++) {
    if (sv.lightstyles[i])
      fprintf(f, "%s\n", sv.lightstyles[i]);
    else
      fprintf(f, "m\n");
  }

  ED_WriteGlobals(f);
  for (i = 0; i < qcvm->num_edicts; i++) {
    if (i > 0 && i <= svs.maxclients && !svs.clients[i - 1].active)
      fprintf(f, "{\n}\n");
    else
      ED_Write(f, EDICT_NUM(i));
    fflush(f);
  }
  write_failed = ferror(f) != 0 || fflush(f) != 0;
  if (fclose(f) != 0)
    write_failed = true;

  if (switched_qcvm)
    PR_SwitchQCVM(NULL);

  if (write_failed || !Host_SavegameReplaceFile(tempname, name)) {
    remove(tempname);
    if (!quiet)
      Con_Printf("ERROR: couldn't finalize savegame.\n");
    else
      Con_DPrintf("Coop autosave: couldn't finalize %s\n", name);
    return false;
  }

  Host_LoadgameDiscardPendingClients(quiet);
  if (!quiet)
    Con_Printf("done.\n");
  return true;
}

static void Host_LoadgameMaybeClearLoadedFlag(void) {
  int i;

  for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++) {
    if (sv.loadgame_client_saved[i])
      return;
  }
  sv.loadgame = false;
  sv.paused = false;
  memset(sv.loadgame_client_name_required, 0,
         sizeof(sv.loadgame_client_name_required));
  memset(sv.loadgame_client_names, 0, sizeof(sv.loadgame_client_names));
  memset(sv.loadgame_client_spawn_parms, 0,
         sizeof(sv.loadgame_client_spawn_parms));
  memset(sv.loadgame_client_colors, 0, sizeof(sv.loadgame_client_colors));
  memset(sv.loadgame_client_old_frags, 0,
         sizeof(sv.loadgame_client_old_frags));
}

static qboolean Host_LoadgameHasPendingClients(void) {
  int i;

  for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++) {
    if (sv.loadgame_client_saved[i])
      return true;
  }
  return false;
}

static void Host_LoadgameDiscardPendingClients(qboolean quiet) {
  int i;

  if (!Host_LoadgameHasPendingClients())
    return;

  for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++)
    sv.loadgame_client_saved[i] = false;
  memset(sv.loadgame_client_name_required, 0,
         sizeof(sv.loadgame_client_name_required));
  memset(sv.loadgame_client_names, 0, sizeof(sv.loadgame_client_names));
  memset(sv.loadgame_client_spawn_parms, 0,
         sizeof(sv.loadgame_client_spawn_parms));
  memset(sv.loadgame_client_colors, 0, sizeof(sv.loadgame_client_colors));
  memset(sv.loadgame_client_old_frags, 0,
         sizeof(sv.loadgame_client_old_frags));
  sv.loadgame = false;
  sv.paused = false;
  if (!quiet)
    Con_Printf("Discarded pending saved co-op player states.\n");
}

static byte *Host_LoadgameClientEdictSnapshot(int clientnum) {
  if (!sv.loadgame_client_edicts)
    sv.loadgame_client_edicts =
        (byte *)Hunk_AllocName(MAX_SCOREBOARD * qcvm->edict_size, "loadclients");

  return sv.loadgame_client_edicts + clientnum * qcvm->edict_size;
}

static void Host_LoadgameRestoreClientEdict(int clientnum, edict_t *ent) {
  memcpy(ent, Host_LoadgameClientEdictSnapshot(clientnum), qcvm->edict_size);
  ent->free = false;
  ClearLink(&ent->area);
}

static qboolean Host_LoadgameSavedNameMatches(int clientnum,
                                              const char *client_name) {
  return client_name && client_name[0] &&
         q_strcasecmp(client_name, "unconnected") &&
         sv.loadgame_client_names[clientnum][0] &&
         !q_strcasecmp(sv.loadgame_client_names[clientnum], client_name);
}

static int Host_LoadgameFindSavedClientForSpawn(int clientnum,
                                                const char *client_name) {
  int i;

  /* A single-player save has no identity ambiguity.  Changing _cl_name must
     not turn a quickload into a fresh mid-map ClientConnect spawn. */
  if (svs.maxclients == 1 && sv.loadgame_client_saved[0])
    return 0;

  if (clientnum >= 0 && clientnum < MAX_SCOREBOARD &&
      sv.loadgame_client_saved[clientnum] &&
      Host_LoadgameSavedNameMatches(clientnum, client_name))
    return clientnum;

  if (client_name && client_name[0] &&
      q_strcasecmp(client_name, "unconnected")) {
    for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++) {
      if (!sv.loadgame_client_saved[i] || !sv.loadgame_client_names[i][0])
        continue;
      if (!q_strcasecmp(sv.loadgame_client_names[i], client_name))
        return i;
    }
  }

  if (clientnum >= 0 && clientnum < MAX_SCOREBOARD &&
      sv.loadgame_client_saved[clientnum])
    return sv.loadgame_client_name_required[clientnum] ? -1 : clientnum;

  return -1;
}

void Host_CoopAutosaveFrame(void) {
  int found_secrets;
  int killed_monsters;
  int kill_interval;
  int kill_bucket;
  int serverflags;
  int slots;
  float min_interval;
  const char *reason;
  char savename[MAX_QPATH];

  if (!sv.active || sv.state != ss_active || sv.paused ||
      svs.maxclients <= 1 || !coop.value || deathmatch.value ||
      !sv_save_multiplayer.value ||
      !SV_CoopFeatureEnabled(&sv_coop_autosave, true) ||
      Host_LoadgameHasPendingClients()) {
    sv.coop_autosave_initialized = false;
    return;
  }

  if (Host_SavegameActiveClients() <= 0)
    return;

  found_secrets = (int)pr_global_struct->found_secrets;
  killed_monsters = (int)pr_global_struct->killed_monsters;
  kill_interval = (int)sv_coop_autosave_kill_interval.value;
  if (kill_interval < 1)
    kill_interval = 1;
  kill_bucket = killed_monsters / kill_interval;
  serverflags = (int)pr_global_struct->serverflags;

  if (!sv.coop_autosave_initialized) {
    sv.coop_autosave_initialized = true;
    sv.coop_autosave_mapstart_done = false;
    sv.coop_autosave_last_time = 0;
    sv.coop_autosave_last_realtime = 0;
    sv.coop_autosave_last_secrets = found_secrets;
    sv.coop_autosave_last_kill_bucket = kill_bucket;
    sv.coop_autosave_last_serverflags = serverflags;
  }

  reason = NULL;
  if (!sv.coop_autosave_mapstart_done &&
      qcvm->time >= COOP_AUTOSAVE_MAPSTART_DELAY) {
    reason = "map start";
  } else if (found_secrets > sv.coop_autosave_last_secrets) {
    reason = "secret";
  } else if (kill_bucket > sv.coop_autosave_last_kill_bucket) {
    reason = "monster progress";
  } else if (serverflags != sv.coop_autosave_last_serverflags) {
    reason = "serverflags";
  }

  if (!reason)
    return;

  min_interval = sv_coop_autosave_min_interval.value;
  if (min_interval < 0)
    min_interval = 0;
  if (sv.coop_autosave_last_realtime > 0 &&
      realtime - sv.coop_autosave_last_realtime < min_interval)
    return;

  slots = (int)sv_coop_autosave_slots.value;
  if (slots < 1)
    slots = 1;
  if (slots > COOP_AUTOSAVE_MAX_SLOTS)
    slots = COOP_AUTOSAVE_MAX_SLOTS;

  q_snprintf(savename, sizeof(savename), "coop_auto%i",
             sv.coop_autosave_next_slot % slots);
  if (!Host_SavegameWrite(savename, true))
    return;

  Con_Printf("Coop autosaved %s.sav (%s).\n", savename, reason);
  sv.coop_autosave_next_slot = (sv.coop_autosave_next_slot + 1) % slots;
  sv.coop_autosave_last_time = qcvm->time;
  sv.coop_autosave_last_realtime = realtime;
  sv.coop_autosave_last_secrets = found_secrets;
  sv.coop_autosave_last_kill_bucket = kill_bucket;
  sv.coop_autosave_last_serverflags = serverflags;
  if (!strcmp(reason, "map start"))
    sv.coop_autosave_mapstart_done = true;
}

/*
===============
Host_Savegame_f
===============
*/
static void Host_Savegame_f(void) {
  if (cmd_source != src_command)
    return;

  if (Cmd_Argc() != 2) {
    Con_Printf("save <savename> : save a game\n");
    return;
  }

  Host_SavegameWrite(Cmd_Argv(1), false);
}

/*
===============
Host_Loadgame_f
===============
*/
static void Host_Loadgame_f(void) {
  static char *start;

  char name[MAX_OSPATH];
  char mapname[MAX_QPATH];
  float time, tfloat;
  const char *data, *validated_data;
  int i;
  int j;
  edict_t *ent;
  int entnum;
  int version;
  int saved_maxclients;
  int active;
  qboolean saved_active[MAX_SCOREBOARD];
  char saved_names[MAX_SCOREBOARD][MAX_SCOREBOARDNAME];
  int saved_colors[MAX_SCOREBOARD];
  int saved_frags[MAX_SCOREBOARD];
  float spawn_parms[MAX_SCOREBOARD][NUM_SPAWN_PARMS];
  char encoded_name[MAX_SCOREBOARDNAME * 2];

  if (cmd_source != src_command)
    return;

  if (Cmd_Argc() != 2) {
    Con_Printf("load <savename> : load a game\n");
    return;
  }

  if (strstr(Cmd_Argv(1), "..")) {
    Con_Printf("Relative pathnames are not allowed.\n");
    return;
  }

  cls.demonum = -1; // stop demo loop in case this fails

  q_snprintf(name, sizeof(name), "%s/%s", com_gamedir, Cmd_Argv(1));
  COM_AddExtension(name, ".sav", sizeof(name));

  // we can't call SCR_BeginLoadingPlaque, because too much stack space has
  // been used.  The menu calls it before stuffing loadgame command
  //	SCR_BeginLoadingPlaque ();

  Con_Printf("Loading game from %s...\n", name);

  // avoid leaking if the previous Host_Loadgame_f failed with a Host_Error
  if (start != NULL)
    free(start);

  start = (char *)COM_LoadMallocFile_TextMode_OSPath(name, NULL);
  if (start == NULL) {
    Con_Printf("ERROR: couldn't open.\n");
    return;
  }

  data = start;
  if (!Host_LoadgameParseInt(&data, &version))
    goto malformed_header;
  if (version != SAVEGAME_VERSION && version != SAVEGAME_MULTICLIENT_VERSION &&
      version != SAVEGAME_LEGACY_VERSION) {
    free(start);
    start = NULL;
    SCR_EndLoadingPlaque();
    Con_Printf("ERROR: savegame is version %i, not %i, %i, or %i.\n", version,
               SAVEGAME_LEGACY_VERSION, SAVEGAME_MULTICLIENT_VERSION,
               SAVEGAME_VERSION);
    return;
  }
  if (!Host_LoadgameParseString(&data))
    goto malformed_header;
  memset(saved_active, 0, sizeof(saved_active));
  memset(saved_names, 0, sizeof(saved_names));
  memset(saved_colors, 0, sizeof(saved_colors));
  memset(saved_frags, 0, sizeof(saved_frags));
  memset(spawn_parms, 0, sizeof(spawn_parms));
  if (version == SAVEGAME_VERSION ||
      version == SAVEGAME_MULTICLIENT_VERSION) {
    if (!Host_LoadgameParseInt(&data, &saved_maxclients))
      goto malformed_header;
    if (saved_maxclients < 1 || saved_maxclients > MAX_SCOREBOARD) {
      free(start);
      start = NULL;
      SCR_EndLoadingPlaque();
      Con_Printf("ERROR: savegame has invalid maxplayers %i.\n",
                 saved_maxclients);
      return;
    }
    if (saved_maxclients != svs.maxclients) {
      free(start);
      start = NULL;
      SCR_EndLoadingPlaque();
      Con_Printf("ERROR: savegame was made with maxplayers %i; current "
                 "maxplayers is %i. Set maxplayers %i before loading.\n",
                 saved_maxclients, svs.maxclients, saved_maxclients);
      return;
    }
    for (i = 0; i < saved_maxclients; i++) {
      if (!Host_LoadgameParseInt(&data, &active))
        goto malformed_header;
      saved_active[i] = active ? true : false;
      if (version == SAVEGAME_VERSION) {
        data = Host_LoadgameReadLine(data, encoded_name,
                                     sizeof(encoded_name));
        if (!data)
          goto malformed_header;
        Host_LoadgameReadClientName(encoded_name, saved_names[i],
                                    sizeof(saved_names[i]));
      }
      if (!Host_LoadgameParseInt(&data, &saved_colors[i]) ||
          !Host_LoadgameParseInt(&data, &saved_frags[i]))
        goto malformed_header;
      for (j = 0; j < NUM_SPAWN_PARMS; j++) {
        if (!Host_LoadgameParseFloat(&data, &spawn_parms[i][j]))
          goto malformed_header;
      }
    }
  } else {
    saved_maxclients = 1;
    saved_active[0] = true;
    for (i = 0; i < NUM_SPAWN_PARMS; i++) {
      if (!Host_LoadgameParseFloat(&data, &spawn_parms[0][i]))
        goto malformed_header;
    }
  }
  // this silliness is so we can load 1.06 save files, which have float skill
  // values
  if (!Host_LoadgameParseFloat(&data, &tfloat))
    goto malformed_header;

  if (!Host_LoadgameParseString(&data))
    goto malformed_header;
  q_strlcpy(mapname, com_token, sizeof(mapname));
  if (!Host_LoadgameParseFloat(&data, &time))
    goto malformed_header;

  /* Validate the fixed-size remainder before destroying the current game. */
  validated_data = data;
  for (i = 0; i < MAX_LIGHTSTYLES; i++) {
    if (!Host_LoadgameParseString(&validated_data))
      goto malformed_header;
  }
  validated_data = COM_Parse(validated_data);
  if (!validated_data || strcmp(com_token, "{"))
    goto malformed_header;

  current_skill = (int)(tfloat + 0.1);
  Cvar_SetValue("skill", (float)current_skill);

  CL_Disconnect_f();

  PR_SwitchQCVM(&sv.qcvm);
  SV_MG3UpgradeResetCampaign();
  SV_SpawnServer(mapname);

  if (!sv.active) {
    PR_SwitchQCVM(NULL);
    free(start);
    start = NULL;
    SCR_EndLoadingPlaque();
    Con_Printf("Couldn't load map\n");
    return;
  }
  sv.paused = true; // pause until all clients connect
  sv.loadgame = true;
  sv.loadgame_resumed = false;
  svs.coop_loadgame_late_join_spawns_near =
      (svs.maxclients > 1 && coop.value && !deathmatch.value);

  // load the light styles
  for (i = 0; i < MAX_LIGHTSTYLES; i++) {
    data = COM_ParseStringNewline(data);
    sv.lightstyles[i] = (const char *)Hunk_Strdup(com_token, "lightstyles");
  }

  // load the edicts out of the savegame file
  entnum = -1; // -1 is the globals
  while (*data) {
    data = COM_Parse(data);
    if (!com_token[0])
      break; // end of file
    if (strcmp(com_token, "{")) {
      Host_Error("First token isn't a brace");
    }

    if (entnum == -1) { // parse the global vars
      data = ED_ParseGlobals(data);
    } else { // parse an edict
      ent = EDICT_NUM(entnum);
      if (entnum < qcvm->num_edicts) {
        ED_ClearEdict(ent);
      } else {
        memset(ent, 0, qcvm->edict_size);
        ent->baseline.scale = ENTSCALE_DEFAULT;
      }
      data = ED_ParseEdict(data, ent);

      // link it into the bsp tree
      if (!ent->free)
        SV_LinkEdict(ent, false);
    }

    entnum++;
  }

  // Clear edicts allocated during map loading but no longer used after
  // restoring saved game state.  Do not ED_Free() these, because they are
  // about to be outside num_edicts and must not be queued for reuse.
  for (i = entnum; i < qcvm->num_edicts; i++)
    ED_ClearEdict(EDICT_NUM(i));

  qcvm->num_edicts = entnum;
  qcvm->time = time;

  free(start);
  start = NULL;

  /* Rebuild MG3's campaign-wide upgrade union before any saved or late
     client is spawned.  Two passes ensure every snapshot receives the full
     union even if an old save contains divergent per-player flags. */
  for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++) {
    if (saved_active[i])
      SV_MG3UpgradeCollectSpawnParms(spawn_parms[i]);
  }
  for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++) {
    if (saved_active[i])
      SV_MG3UpgradeApplySpawnParms(spawn_parms[i]);
  }

  for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++) {
    qboolean has_saved_edict;

    ent = EDICT_NUM(i + 1);
    has_saved_edict = saved_active[i] && !ent->free;
    if (has_saved_edict) {
      saved_frags[i] = (int)ent->v.frags;
      if (!saved_names[i][0] && ent->v.netname)
        q_strlcpy(saved_names[i], PR_GetString(ent->v.netname),
                  sizeof(saved_names[i]));
      memcpy(Host_LoadgameClientEdictSnapshot(i), ent, qcvm->edict_size);
      ED_ClearEdict(ent);
    }

    sv.loadgame_client_saved[i] = has_saved_edict;
    sv.loadgame_client_name_required[i] =
        has_saved_edict && version == SAVEGAME_VERSION && saved_names[i][0] &&
        q_strcasecmp(saved_names[i], "unconnected");
    q_strlcpy(sv.loadgame_client_names[i], saved_names[i],
              sizeof(sv.loadgame_client_names[i]));
    memcpy(sv.loadgame_client_spawn_parms[i], spawn_parms[i],
           sizeof(sv.loadgame_client_spawn_parms[i]));
    sv.loadgame_client_colors[i] = saved_colors[i];
    sv.loadgame_client_old_frags[i] = saved_frags[i];
    svs.clients[i].colors = saved_colors[i];
    svs.clients[i].old_frags = saved_frags[i];
    for (j = 0; j < NUM_SPAWN_PARMS; j++)
      svs.clients[i].spawn_parms[j] = spawn_parms[i][j];
  }

  /* Build the complete shared-progression union before any client runs its
     connection or spawn QC.  Restore order must not affect which consumed
     keys or mod progression the first reconnecting player receives. */
  for (i = 0; i < svs.maxclients && i < MAX_SCOREBOARD; i++) {
    if (sv.loadgame_client_saved[i])
      SV_CoopSharedMergeRestoredClient(
          (edict_t *)Host_LoadgameClientEdictSnapshot(i));
  }
  Host_LoadgameMaybeClearLoadedFlag();

  PR_SwitchQCVM(NULL);

  if (cls.state != ca_dedicated) {
    CL_EstablishConnection("local");
    Host_Reconnect_f();
  }

  if (cls.state != ca_dedicated)
    IN_Activate(); // moved to here from M_Load_Key()
  return;

malformed_header:
  free(start);
  start = NULL;
  SCR_EndLoadingPlaque();
  Con_Printf("ERROR: savegame header is malformed or truncated.\n");
}

//============================================================================

/*
======================
Host_Name_f
======================
*/
static void Host_Name_f(void) {
  char newName[32];

  if (Cmd_Argc() == 1) {
    Con_Printf("\"name\" is \"%s\"\n", cl_name.string);
    return;
  }
  if (Cmd_Argc() == 2)
    q_strlcpy(newName, Cmd_Argv(1), sizeof(newName));
  else
    q_strlcpy(newName, Cmd_Args(), sizeof(newName));
  newName[15] = 0; // client_t structure actually says name[32].

  if (cmd_source == src_command) {
    if (Q_strcmp(cl_name.string, newName) == 0)
      return;
    Cvar_Set("_cl_name", newName);
    if (cls.state == ca_connected)
      Cmd_ForwardToServer();
    return;
  }

  if (host_client->name[0] && strcmp(host_client->name, "unconnected")) {
    if (Q_strcmp(host_client->name, newName) != 0)
      Con_Printf("%s renamed to %s\n", host_client->name, newName);
  }
  Q_strcpy(host_client->name, newName);
  host_client->edict->v.netname = PR_SetEngineString(host_client->name);

  // send notification to all clients
  MSG_WriteByte(&sv.reliable_datagram, svc_updatename);
  MSG_WriteByte(&sv.reliable_datagram, host_client - svs.clients);
  MSG_WriteString(&sv.reliable_datagram, host_client->name);
}

static void Host_Say(qboolean teamonly) {
  int j;
  client_t *client;
  client_t *save;
  const char *p;
  char text[MAXCMDLINE], *p2;
  qboolean quoted;
  qboolean fromServer = false;

  if (cmd_source == src_command) {
    if (cls.state != ca_dedicated) {
      Cmd_ForwardToServer();
      return;
    }
    fromServer = true;
    teamonly = false;
  }

  if (Cmd_Argc() < 2)
    return;

  save = host_client;

  p = Cmd_Args();
  // remove quotes if present
  quoted = false;
  if (*p == '\"') {
    p++;
    quoted = true;
  }
  // turn on color set 1
  if (!fromServer)
    q_snprintf(text, sizeof(text), "\001%s: %s", save->name, p);
  else
    q_snprintf(text, sizeof(text), "\001<%s> %s", hostname.string, p);

  // check length & truncate if necessary
  j = (int)strlen(text);
  if (j >= (int)sizeof(text) - 1) {
    text[sizeof(text) - 2] = '\n';
    text[sizeof(text) - 1] = '\0';
  } else {
    p2 = text + j;
    while ((const char *)p2 > (const char *)text &&
           (p2[-1] == '\r' || p2[-1] == '\n' || (p2[-1] == '\"' && quoted))) {
      if (p2[-1] == '\"' && quoted)
        quoted = false;
      p2[-1] = '\0';
      p2--;
    }
    p2[0] = '\n';
    p2[1] = '\0';
  }

  for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
    if (!client || !client->active || !client->spawned)
      continue;
    if (teamplay.value && teamonly &&
        client->edict->v.team != save->edict->v.team)
      continue;
    host_client = client;
    SV_ClientPrintf("%s", text);
  }
  host_client = save;

  if (cls.state == ca_dedicated)
    Sys_Printf("%s", &text[1]);
}

static void Host_Say_f(void) { Host_Say(false); }

static void Host_Say_Team_f(void) { Host_Say(true); }

static void Host_Tell_f(void) {
  int j;
  client_t *client;
  client_t *save;
  const char *p;
  char text[MAXCMDLINE], *p2;
  qboolean quoted;

  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (Cmd_Argc() < 3)
    return;

  p = Cmd_Args();
  // remove quotes if present
  quoted = false;
  if (*p == '\"') {
    p++;
    quoted = true;
  }
  q_snprintf(text, sizeof(text), "%s: %s", host_client->name, p);

  // check length & truncate if necessary
  j = (int)strlen(text);
  if (j >= (int)sizeof(text) - 1) {
    text[sizeof(text) - 2] = '\n';
    text[sizeof(text) - 1] = '\0';
  } else {
    p2 = text + j;
    while ((const char *)p2 > (const char *)text &&
           (p2[-1] == '\r' || p2[-1] == '\n' || (p2[-1] == '\"' && quoted))) {
      if (p2[-1] == '\"' && quoted)
        quoted = false;
      p2[-1] = '\0';
      p2--;
    }
    p2[0] = '\n';
    p2[1] = '\0';
  }

  save = host_client;
  for (j = 0, client = svs.clients; j < svs.maxclients; j++, client++) {
    if (!client->active || !client->spawned)
      continue;
    if (q_strcasecmp(client->name, Cmd_Argv(1)))
      continue;
    host_client = client;
    SV_ClientPrintf("%s", text);
    break;
  }
  host_client = save;
}

/*
==================
Host_Color_f
==================
*/
static void Host_Color_f(void) {
  int top, bottom;
  int playercolor;

  if (Cmd_Argc() == 1) {
    Con_Printf("\"color\" is \"%i %i\"\n", ((int)cl_color.value) >> 4,
               ((int)cl_color.value) & 0x0f);
    Con_Printf("color <0-13> [0-13]\n");
    return;
  }

  if (Cmd_Argc() == 2)
    top = bottom = atoi(Cmd_Argv(1));
  else {
    top = atoi(Cmd_Argv(1));
    bottom = atoi(Cmd_Argv(2));
  }

  top &= 15;
  if (top > 13)
    top = 13;
  bottom &= 15;
  if (bottom > 13)
    bottom = 13;

  playercolor = top * 16 + bottom;

  if (cmd_source == src_command) {
    Cvar_SetValue("_cl_color", playercolor);
    if (cls.state == ca_connected)
      Cmd_ForwardToServer();
    return;
  }

  host_client->colors = playercolor;
  host_client->edict->v.team = bottom + 1;

  // send notification to all clients
  MSG_WriteByte(&sv.reliable_datagram, svc_updatecolors);
  MSG_WriteByte(&sv.reliable_datagram, host_client - svs.clients);
  MSG_WriteByte(&sv.reliable_datagram, host_client->colors);
}

/*
==================
Host_Kill_f
==================
*/
static void Host_Kill_f(void) {
  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (sv_player->v.health <= 0) {
    SV_ClientPrintf("Can't suicide -- already dead!\n");
    return;
  }

  pr_global_struct->time = qcvm->time;
  pr_global_struct->self = EDICT_TO_PROG(sv_player);
  PR_ExecuteProgram(pr_global_struct->ClientKill);
}

/*
==================
Host_Pause_f
==================
*/
static void Host_Pause_f(void) {
  // ericw -- demo pause support (inspired by MarkV)
  if (cls.demoplayback) {
    cls.demopaused = !cls.demopaused;
    cl.paused = cls.demopaused;
    return;
  }

  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }
  if (!pausable.value)
    SV_ClientPrintf("Pause not allowed.\n");
  else {
    sv.paused ^= 1;

    if (sv.paused) {
      SV_BroadcastPrintf("%s paused the game\n",
                         PR_GetString(sv_player->v.netname));
    } else {
      SV_BroadcastPrintf("%s unpaused the game\n",
                         PR_GetString(sv_player->v.netname));
    }

    // send notification to all clients
    MSG_WriteByte(&sv.reliable_datagram, svc_setpause);
    MSG_WriteByte(&sv.reliable_datagram, sv.paused);
  }
}

//===========================================================================

/*
==================
Host_PreSpawn_f
==================
*/
static void Host_PreSpawn_f(void) {
  if (cmd_source == src_command) {
    Con_Printf("prespawn is not valid from the console\n");
    return;
  }

  if (host_client->spawned) {
    Con_Printf("prespawn not valid -- already spawned\n");
    return;
  }

  host_client->sendsignon = PRESPAWN_MODELS;
  host_client->signonidx = 0;
}

static void Host_EnableCSQC_f(void) {
  size_t e;

  if (cmd_source != src_client)
    return;

  host_client->csqcactive = true;
  for (e = 1; e < host_client->numpendingcsqcentities; e++)
    if (host_client->pendingcsqcentities_bits[e] & SENDFLAG_PRESENT)
      host_client->pendingcsqcentities_bits[e] |= SENDFLAG_USABLE;
}

static void Host_DisableCSQC_f(void) {
  if (cmd_source != src_client)
    return;
  host_client->csqcactive = false;
}

/*
==================
Host_Spawn_f
==================
*/
static void Host_Spawn_f(void) {
  int i;
  int clientnum;
  int saved_clientnum;
  client_t *client;
  edict_t *ent;
  edict_t *saved_ent;
  qboolean loaded_client;
  qboolean respawn_loaded_client;
  qboolean initial_spawn_client;

  if (cmd_source == src_command) {
    Con_Printf("spawn is not valid from the console\n");
    return;
  }

  if (host_client->spawned) {
    Con_Printf("Spawn not valid -- already spawned\n");
    return;
  }

  host_client->knowntoqc = true;
  host_client->lastmovetime = qcvm->time;

  clientnum = host_client - svs.clients;
  saved_clientnum = -1;
  if (sv.loadgame && sv.loadgame_client_edicts)
    saved_clientnum =
        Host_LoadgameFindSavedClientForSpawn(clientnum, host_client->name);
  loaded_client = saved_clientnum >= 0;
  respawn_loaded_client = false;
  saved_ent = NULL;
  if (loaded_client) {
    saved_ent = (edict_t *)Host_LoadgameClientEdictSnapshot(saved_clientnum);
    respawn_loaded_client = saved_ent->v.health <= 0 ||
                            saved_ent->v.deadflag != DEAD_NO;
  }
  initial_spawn_client = clientnum >= 0 && clientnum < MAX_SCOREBOARD &&
      svs.coop_initial_spawn_client[clientnum];

  // run the entrance script
  if (loaded_client && !respawn_loaded_client) {
    // Living saved client edicts are fully initialized already.
    ent = host_client->edict;
    memcpy(host_client->spawn_parms,
           sv.loadgame_client_spawn_parms[saved_clientnum],
           sizeof(host_client->spawn_parms));
    SV_MG3UpgradeApplySpawnParms(host_client->spawn_parms);
    host_client->colors = sv.loadgame_client_colors[saved_clientnum];
    host_client->old_frags = sv.loadgame_client_old_frags[saved_clientnum];
    Host_LoadgameRestoreClientEdict(saved_clientnum, ent);
    ent->v.netname = PR_SetEngineString(host_client->name);
    ent->v.colormap = NUM_FOR_EDICT(ent);
    ent->v.team = (host_client->colors & 15) + 1;
    host_client->old_frags = (int)ent->v.frags;
    SV_LinkEdict(ent, false);
    sv.loadgame_client_saved[saved_clientnum] = false;
    sv.loadgame_client_name_required[saved_clientnum] = false;
    sv.loadgame_client_names[saved_clientnum][0] = 0;
    memset(sv.loadgame_client_spawn_parms[saved_clientnum], 0,
           sizeof(sv.loadgame_client_spawn_parms[saved_clientnum]));
    sv.loadgame_client_colors[saved_clientnum] = 0;
    sv.loadgame_client_old_frags[saved_clientnum] = 0;
    if (clientnum >= 0 && clientnum < MAX_SCOREBOARD)
      svs.coop_initial_spawn_client[clientnum] = false;
    sv.loadgame_resumed = true;
    Host_LoadgameMaybeClearLoadedFlag();
    sv.paused = false;
  } else {
    if (respawn_loaded_client) {
      memcpy(host_client->spawn_parms,
             sv.loadgame_client_spawn_parms[saved_clientnum],
             sizeof(host_client->spawn_parms));
      SV_MG3UpgradeApplySpawnParms(host_client->spawn_parms);
      host_client->colors = sv.loadgame_client_colors[saved_clientnum];
      host_client->old_frags = sv.loadgame_client_old_frags[saved_clientnum];
    }

    // set up the edict
    ent = host_client->edict;

    ent->free = false;
    memset(&ent->v, 0, qcvm->progs->entityfields * 4);
    ent->v.colormap = NUM_FOR_EDICT(ent);
    ent->v.team = (host_client->colors & 15) + 1;
    ent->v.netname = PR_SetEngineString(host_client->name);

    // copy spawn parms out of the client_t
    SV_MG3UpgradeApplySpawnParms(host_client->spawn_parms);
    for (i = 0; i < NUM_SPAWN_PARMS; i++)
      (&pr_global_struct->parm1)[i] = host_client->spawn_parms[i];
    // call the spawn function
    pr_global_struct->time = qcvm->time;
    pr_global_struct->self = EDICT_TO_PROG(sv_player);
    PR_ExecuteProgram(pr_global_struct->ClientConnect);

    if ((Sys_DoubleTime() - NET_QSocketGetTime(host_client->netconnection)) <=
        qcvm->time)
      Sys_Printf("%s entered the game\n", host_client->name);

    PR_ExecuteProgram(pr_global_struct->PutClientInServer);
    if (respawn_loaded_client) {
      ent->v.frags = host_client->old_frags;
      sv.loadgame_client_saved[saved_clientnum] = false;
      sv.loadgame_client_name_required[saved_clientnum] = false;
      sv.loadgame_client_names[saved_clientnum][0] = 0;
      memset(sv.loadgame_client_spawn_parms[saved_clientnum], 0,
             sizeof(sv.loadgame_client_spawn_parms[saved_clientnum]));
      sv.loadgame_client_colors[saved_clientnum] = 0;
      sv.loadgame_client_old_frags[saved_clientnum] = 0;
    }
    if (clientnum >= 0 && clientnum < MAX_SCOREBOARD)
      svs.coop_initial_spawn_client[clientnum] = false;
    if ((svs.coop_loadgame_late_join_spawns_near || !initial_spawn_client) &&
        SV_CoopFeatureEnabled(&sv_coop_respawn_near_player, true))
      SV_CoopRespawnPlaceNearPlayer(ent);
    if (sv.loadgame) {
      sv.loadgame_resumed = true;
      Host_LoadgameMaybeClearLoadedFlag();
      sv.paused = false;
    }
  }

  // send all current names, colors, and frag counts
  SZ_Clear(&host_client->message);

  // send time of update
  MSG_WriteByte(&host_client->message, svc_time);
  MSG_WriteFloat(&host_client->message, qcvm->time);

  for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++) {
    MSG_WriteByte(&host_client->message, svc_updatename);
    MSG_WriteByte(&host_client->message, i);
    MSG_WriteString(&host_client->message, client->name);
    MSG_WriteByte(&host_client->message, svc_updatefrags);
    MSG_WriteByte(&host_client->message, i);
    MSG_WriteShort(&host_client->message, client->old_frags);
    MSG_WriteByte(&host_client->message, svc_updatecolors);
    MSG_WriteByte(&host_client->message, i);
    MSG_WriteByte(&host_client->message, client->colors);
  }

  /* This snapshot is sent after the client's signon data is rebuilt, so an
     early capability declaration cannot be lost when this buffer is cleared. */
  SV_SendAvatarTable(host_client);

  // send all current light styles
  for (i = 0; i < MAX_LIGHTSTYLES; i++) {
    MSG_WriteByte(&host_client->message, svc_lightstyle);
    MSG_WriteByte(&host_client->message, (char)i);
    MSG_WriteString(&host_client->message, sv.lightstyles[i]);
  }

  //
  // send some stats
  //
  MSG_WriteByte(&host_client->message, svc_updatestat);
  MSG_WriteByte(&host_client->message, STAT_TOTALSECRETS);
  MSG_WriteLong(&host_client->message, pr_global_struct->total_secrets);

  MSG_WriteByte(&host_client->message, svc_updatestat);
  MSG_WriteByte(&host_client->message, STAT_TOTALMONSTERS);
  MSG_WriteLong(&host_client->message, pr_global_struct->total_monsters);

  MSG_WriteByte(&host_client->message, svc_updatestat);
  MSG_WriteByte(&host_client->message, STAT_SECRETS);
  MSG_WriteLong(&host_client->message, pr_global_struct->found_secrets);

  MSG_WriteByte(&host_client->message, svc_updatestat);
  MSG_WriteByte(&host_client->message, STAT_MONSTERS);
  MSG_WriteLong(&host_client->message, pr_global_struct->killed_monsters);

  //
  // send a fixangle
  // Never send a roll angle, because savegames can catch the server
  // in a state where it is expecting the client to correct the angle
  // and it won't happen if the game was just loaded, so you wind up
  // with a permanent head tilt
  ent = EDICT_NUM(1 + (host_client - svs.clients));
  MSG_WriteByte(&host_client->message, svc_setangle);
  for (i = 0; i < 2; i++)
    MSG_WriteAngle(&host_client->message, ent->v.angles[i], sv.protocolflags);
  MSG_WriteAngle(&host_client->message, 0, sv.protocolflags);

  SV_WriteClientdataToMessage(sv_player, &host_client->message);

  MSG_WriteByte(&host_client->message, svc_signonnum);
  MSG_WriteByte(&host_client->message, 3);
  host_client->sendsignon = PRESPAWN_FLUSH;
}

/*
==================
Host_Begin_f
==================
*/
static void Host_Begin_f(void) {
  if (cmd_source == src_command) {
    Con_Printf("begin is not valid from the console\n");
    return;
  }

  host_client->spawned = true;
  SV_CoopSharedApplyToJoiningClient(sv_player);
}

/*
==================
Host_CoopTeleportPlayer_f

Co-op client helper used by the weapon wheel player list.
Usage: coop_teleport_player <1-based player slot>
==================
*/
static void Host_CoopTeleportPlayer_f(void) {
  int slot;
  client_t *target_client;

  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (!coop.value || pr_global_struct->deathmatch)
    return;
  if (!host_client || !host_client->active || !host_client->spawned ||
      !sv_player)
    return;
  if (Cmd_Argc() < 2)
    return;

  slot = Q_atoi(Cmd_Argv(1)) - 1;
  if (slot < 0 || slot >= svs.maxclients)
    return;

  target_client = &svs.clients[slot];
  if (!target_client->active || !target_client->spawned ||
      !target_client->edict || target_client == host_client)
    return;

  if (!SV_CoopRespawnTeleportToPlayer(sv_player, target_client->edict))
    SV_ClientPrintf("No safe teleport spot near %s\n", target_client->name);
}

static edict_t *Host_CoopFindSpawnClass(const char *classname) {
  int i;

  for (i = svs.maxclients + 1; i < qcvm->num_edicts; i++) {
    edict_t *ent = EDICT_NUM(i);
    if (!ent->free && ent->v.classname &&
        !q_strcasecmp(PR_GetString(ent->v.classname), classname))
      return ent;
  }

  return NULL;
}

static edict_t *Host_CoopSelectSpawnPoint(void) {
  dfunction_t *func;
  edict_t *spawn = NULL;
  int old_self, old_other, spawnprog;
  int old_return[3];
  float old_time;

  func = ED_FindFunction("SelectSpawnPoint");
  if (func && func->numparms == 0) {
    old_self = pr_global_struct->self;
    old_other = pr_global_struct->other;
    old_time = pr_global_struct->time;
    memcpy(old_return, &qcvm->globals[OFS_RETURN], sizeof(old_return));

    pr_global_struct->self = EDICT_TO_PROG(sv_player);
    pr_global_struct->other = EDICT_TO_PROG(qcvm->edicts);
    pr_global_struct->time = qcvm->time;
    G_INT(OFS_RETURN) = 0;
    PR_ExecuteProgram(func - qcvm->functions);
    spawnprog = G_INT(OFS_RETURN);

    pr_global_struct->self = old_self;
    pr_global_struct->other = old_other;
    pr_global_struct->time = old_time;
    memcpy(&qcvm->globals[OFS_RETURN], old_return, sizeof(old_return));

    if (spawnprog > svs.maxclients * qcvm->edict_size &&
        spawnprog < qcvm->num_edicts * qcvm->edict_size &&
        spawnprog % qcvm->edict_size == 0) {
      spawn = PROG_TO_EDICT(spawnprog);
      if (spawn->free)
        spawn = NULL;
    }
  }

  if (!spawn)
    spawn = Host_CoopFindSpawnClass("info_player_coop");
  if (!spawn)
    spawn = Host_CoopFindSpawnClass("info_player_start");
  return spawn;
}

/*
==================
Host_CoopTeleportSpawn_f

Co-op weapon-wheel helper that asks QuakeC for the active map's proper spawn
point, then safely relocates the requesting player without killing them.
==================
*/
static void Host_CoopTeleportSpawn_f(void) {
  edict_t *spawn;

  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (!coop.value || pr_global_struct->deathmatch || !host_client ||
      !host_client->active || !host_client->spawned || !sv_player)
    return;
  if (sv_player->v.health <= 0 || sv_player->v.deadflag != DEAD_NO ||
      sv_player->v.solid == SOLID_NOT)
    return;

  spawn = Host_CoopSelectSpawnPoint();
  if (!spawn) {
    SV_ClientPrintf("No player spawn point is available\n");
    return;
  }

  if (!SV_CoopRespawnTeleportToSpawn(sv_player, spawn))
    SV_ClientPrintf("No safe player spawn position is available\n");
}

//===========================================================================

/*
==================
Host_Kick_f

Kicks a user off of the server
==================
*/
static void Host_Kick_f(void) {
  const char *who;
  const char *message = NULL;
  client_t *save;
  int i;
  qboolean byNumber = false;

  if (cmd_source == src_command) {
    if (!sv.active) {
      Cmd_ForwardToServer();
      return;
    }
  } else if (pr_global_struct->deathmatch)
    return;

  save = host_client;

  if (Cmd_Argc() > 2 && Q_strcmp(Cmd_Argv(1), "#") == 0) {
    i = Q_atof(Cmd_Argv(2)) - 1;
    if (i < 0 || i >= svs.maxclients)
      return;
    if (!svs.clients[i].active)
      return;
    host_client = &svs.clients[i];
    byNumber = true;
  } else {
    for (i = 0, host_client = svs.clients; i < svs.maxclients;
         i++, host_client++) {
      if (!host_client->active)
        continue;
      if (q_strcasecmp(host_client->name, Cmd_Argv(1)) == 0)
        break;
    }
  }

  if (i < svs.maxclients) {
    if (cmd_source == src_command)
      if (cls.state == ca_dedicated)
        who = "Console";
      else
        who = cl_name.string;
    else
      who = save->name;

    // A remote client cannot kick itself.  A dedicated console has no client
    // identity; host_client merely points at whichever client ran last.
    if (cmd_source != src_command && host_client == save)
      return;

    if (Cmd_Argc() > 2) {
      message = COM_Parse(Cmd_Args());
      if (byNumber) {
        message++;              // skip the #
        while (*message == ' ') // skip white space
          message++;
        message += strlen(Cmd_Argv(2)); // skip the number
      }
      while (*message && *message == ' ')
        message++;
    }
    if (message)
      SV_ClientPrintf("Kicked by %s: %s\n", who, message);
    else
      SV_ClientPrintf("Kicked by %s\n", who);
    SV_DropClient(false);
  }

  host_client = save;
}

/*
===============================================================================

DEBUGGING TOOLS

===============================================================================
*/

static qboolean Host_IsLocalClientCommand(void) {
  return cmd_source == src_client && host_client && host_client->netconnection &&
         Q_strcmp(NET_QSocketGetAddressString(host_client->netconnection),
                  "LOCAL") == 0;
}

/*
==================
Host_Give_f
==================
*/
static void Host_Give_f(void) {
  const char *t;
  int v;
  eval_t *val;

  if (cmd_source == src_command) {
    Cmd_ForwardToServer();
    return;
  }

  if (!Host_IsLocalClientCommand()) {
    Con_DPrintf("%s tried to give\n", host_client ? host_client->name : "client");
    return;
  }

  if (pr_global_struct->deathmatch)
    return;

  t = Cmd_Argv(1);
  v = atoi(Cmd_Argv(2));

  switch (t[0]) {
  case '0':
  case '1':
  case '2':
  case '3':
  case '4':
  case '5':
  case '6':
  case '7':
  case '8':
  case '9':
    // MED 01/04/97 added hipnotic give stuff
    if (hipnotic) {
      if (t[0] == '6') {
        if (t[1] == 'a')
          sv_player->v.items = (int)sv_player->v.items | HIT_PROXIMITY_GUN;
        else
          sv_player->v.items = (int)sv_player->v.items | IT_GRENADE_LAUNCHER;
      } else if (t[0] == '9')
        sv_player->v.items = (int)sv_player->v.items | HIT_LASER_CANNON;
      else if (t[0] == '0')
        sv_player->v.items = (int)sv_player->v.items | HIT_MJOLNIR;
      else if (t[0] >= '2')
        sv_player->v.items =
            (int)sv_player->v.items | (IT_SHOTGUN << (t[0] - '2'));
    } else {
      if (t[0] >= '2')
        sv_player->v.items =
            (int)sv_player->v.items | (IT_SHOTGUN << (t[0] - '2'));
    }
    break;

  case 's':
    if (rogue) {
      val = GetEdictFieldValueByName(sv_player, "ammo_shells1");
      if (val)
        val->_float = v;
    }
    sv_player->v.ammo_shells = v;
    break;

  case 'n':
    if (rogue) {
      val = GetEdictFieldValueByName(sv_player, "ammo_nails1");
      if (val) {
        val->_float = v;
        if (sv_player->v.weapon <= IT_LIGHTNING)
          sv_player->v.ammo_nails = v;
      }
    } else {
      sv_player->v.ammo_nails = v;
    }
    break;

  case 'l':
    if (rogue) {
      val = GetEdictFieldValueByName(sv_player, "ammo_lava_nails");
      if (val) {
        val->_float = v;
        if (sv_player->v.weapon > IT_LIGHTNING)
          sv_player->v.ammo_nails = v;
      }
    }
    break;

  case 'r':
    if (rogue) {
      val = GetEdictFieldValueByName(sv_player, "ammo_rockets1");
      if (val) {
        val->_float = v;
        if (sv_player->v.weapon <= IT_LIGHTNING)
          sv_player->v.ammo_rockets = v;
      }
    } else {
      sv_player->v.ammo_rockets = v;
    }
    break;

  case 'm':
    if (rogue) {
      val = GetEdictFieldValueByName(sv_player, "ammo_multi_rockets");
      if (val) {
        val->_float = v;
        if (sv_player->v.weapon > IT_LIGHTNING)
          sv_player->v.ammo_rockets = v;
      }
    }
    break;

  case 'h':
    sv_player->v.health = v;
    break;

  case 'c':
    if (rogue) {
      val = GetEdictFieldValueByName(sv_player, "ammo_cells1");
      if (val) {
        val->_float = v;
        if (sv_player->v.weapon <= IT_LIGHTNING)
          sv_player->v.ammo_cells = v;
      }
    } else {
      sv_player->v.ammo_cells = v;
    }
    break;

  case 'p':
    if (rogue) {
      val = GetEdictFieldValueByName(sv_player, "ammo_plasma");
      if (val) {
        val->_float = v;
        if (sv_player->v.weapon > IT_LIGHTNING)
          sv_player->v.ammo_cells = v;
      }
    }
    break;

  // johnfitz -- give armour
  case 'a':
    if (v > 150) {
      sv_player->v.armortype = 0.8;
      sv_player->v.armorvalue = v;
      sv_player->v.items = sv_player->v.items -
                           ((int)(sv_player->v.items) &
                            (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
                           IT_ARMOR3;
    } else if (v > 100) {
      sv_player->v.armortype = 0.6;
      sv_player->v.armorvalue = v;
      sv_player->v.items = sv_player->v.items -
                           ((int)(sv_player->v.items) &
                            (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
                           IT_ARMOR2;
    } else if (v >= 0) {
      sv_player->v.armortype = 0.3;
      sv_player->v.armorvalue = v;
      sv_player->v.items = sv_player->v.items -
                           ((int)(sv_player->v.items) &
                            (int)(IT_ARMOR1 | IT_ARMOR2 | IT_ARMOR3)) +
                           IT_ARMOR1;
    }
    break;
    // johnfitz
  }

  // johnfitz -- update currentammo to match new ammo (so statusbar updates
  // correctly)
  switch ((int)(sv_player->v.weapon)) {
  case IT_SHOTGUN:
  case IT_SUPER_SHOTGUN:
    sv_player->v.currentammo = sv_player->v.ammo_shells;
    break;
  case IT_NAILGUN:
  case IT_SUPER_NAILGUN:
  case RIT_LAVA_SUPER_NAILGUN:
    sv_player->v.currentammo = sv_player->v.ammo_nails;
    break;
  case IT_GRENADE_LAUNCHER:
  case IT_ROCKET_LAUNCHER:
  case RIT_MULTI_GRENADE:
  case RIT_MULTI_ROCKET:
    sv_player->v.currentammo = sv_player->v.ammo_rockets;
    break;
  case IT_LIGHTNING:
  case HIT_LASER_CANNON:
  case HIT_MJOLNIR:
    sv_player->v.currentammo = sv_player->v.ammo_cells;
    break;
  case RIT_LAVA_NAILGUN: // same as IT_AXE
    if (rogue)
      sv_player->v.currentammo = sv_player->v.ammo_nails;
    break;
  case RIT_PLASMA_GUN: // same as HIT_PROXIMITY_GUN
    if (rogue)
      sv_player->v.currentammo = sv_player->v.ammo_cells;
    if (hipnotic)
      sv_player->v.currentammo = sv_player->v.ammo_rockets;
    break;
  }
  // johnfitz
}

static client_t *Host_FindClientByCommandArgs(int firstarg) {
  int i;

  if (Cmd_Argc() <= firstarg) {
    if (cmd_source == src_client && host_client && host_client->active)
      return host_client;
    for (i = 0; i < svs.maxclients; i++)
      if (svs.clients[i].active)
        return &svs.clients[i];
    return NULL;
  }

  if (Cmd_Argc() > firstarg + 1 && Q_strcmp(Cmd_Argv(firstarg), "#") == 0) {
    i = Q_atoi(Cmd_Argv(firstarg + 1)) - 1;
    if (i < 0 || i >= svs.maxclients || !svs.clients[i].active)
      return NULL;
    return &svs.clients[i];
  }

  for (i = 0; i < svs.maxclients; i++) {
    if (!svs.clients[i].active)
      continue;
    if (q_strcasecmp(svs.clients[i].name, Cmd_Argv(firstarg)) == 0)
      return &svs.clients[i];
  }

  return NULL;
}

static void Host_GiveAllFallback(client_t *client) {
  const int stock_weapon_bits =
      IT_SHOTGUN | IT_SUPER_SHOTGUN | IT_NAILGUN | IT_SUPER_NAILGUN |
      IT_GRENADE_LAUNCHER | IT_ROCKET_LAUNCHER | IT_LIGHTNING;
  int weapon_bits;
  eval_t *val;

  sv_player = client->edict;

  weapon_bits = stock_weapon_bits | (rogue ? RIT_AXE : IT_AXE);
  if (rogue)
    weapon_bits |= RIT_LAVA_NAILGUN | RIT_LAVA_SUPER_NAILGUN |
                   RIT_MULTI_GRENADE | RIT_MULTI_ROCKET | RIT_PLASMA_GUN;
  if (hipnotic)
    weapon_bits |= HIT_PROXIMITY_GUN | HIT_LASER_CANNON | HIT_MJOLNIR;

  sv_player->v.items = (int)sv_player->v.items | weapon_bits;
  val = GetEdictFieldValueByName(sv_player, "weapons");
  if (val)
    val->_float = (int)val->_float | weapon_bits;

  sv_player->v.weapon = IT_SHOTGUN;
  sv_player->v.ammo_shells = q_max(sv_player->v.ammo_shells, 100);
  sv_player->v.ammo_nails = q_max(sv_player->v.ammo_nails, 200);
  sv_player->v.ammo_rockets = q_max(sv_player->v.ammo_rockets, 100);
  sv_player->v.ammo_cells = q_max(sv_player->v.ammo_cells, 100);
  sv_player->v.currentammo = sv_player->v.ammo_shells;

  val = GetEdictFieldValueByName(sv_player, "ammo_shells1");
  if (val)
    val->_float = q_max(val->_float, 100);
  val = GetEdictFieldValueByName(sv_player, "ammo_nails1");
  if (val)
    val->_float = q_max(val->_float, 200);
  val = GetEdictFieldValueByName(sv_player, "ammo_rockets1");
  if (val)
    val->_float = q_max(val->_float, 100);
  val = GetEdictFieldValueByName(sv_player, "ammo_cells1");
  if (val)
    val->_float = q_max(val->_float, 100);
  val = GetEdictFieldValueByName(sv_player, "ammo_lava_nails");
  if (val)
    val->_float = q_max(val->_float, 200);
  val = GetEdictFieldValueByName(sv_player, "ammo_multi_rockets");
  if (val)
    val->_float = q_max(val->_float, 100);
  val = GetEdictFieldValueByName(sv_player, "ammo_plasma");
  if (val)
    val->_float = q_max(val->_float, 100);
}

static void Host_SaveClientSpawnParms(client_t *client) {
  int i;

  if (!pr_global_struct->SetChangeParms)
    return;

  pr_global_struct->self = EDICT_TO_PROG(client->edict);
  PR_ExecuteProgram(pr_global_struct->SetChangeParms);
  for (i = 0; i < NUM_SPAWN_PARMS; i++)
    client->spawn_parms[i] = (&pr_global_struct->parm1)[i];
  SV_MG3UpgradeSyncSpawnParms(client->spawn_parms);
}

static qboolean Host_GiveAllClient(client_t *client) {
  client_t *old_host_client;
  edict_t *old_sv_player;
  dfunction_t *func;

  if (!client || !client->active || !client->edict)
    return false;

  old_host_client = host_client;
  old_sv_player = sv_player;
  host_client = client;
  sv_player = client->edict;

  func = ED_FindFunction("GiveAllCommand");
  if (func) {
    pr_global_struct->self = EDICT_TO_PROG(client->edict);
    pr_global_struct->time = qcvm->time;
    PR_ExecuteProgram(func - qcvm->functions);
    Host_SaveClientSpawnParms(client);
    host_client = old_host_client;
    sv_player = old_sv_player;
    return true;
  }

  Host_GiveAllFallback(client);
  Host_SaveClientSpawnParms(client);

  host_client = old_host_client;
  sv_player = old_sv_player;
  return true;
}

/*
==================
Host_SV_GiveAll_f

Server/admin give-all command for dedicated co-op testing.
Usage: sv_giveall [playername | # slot | all]
==================
*/
static void Host_SV_GiveAll_f(void) {
  client_t *client;
  qcvm_t *old_qcvm;
  int i, count;

  if (!sv.active) {
    if (cmd_source == src_command) {
      Cmd_ForwardToServer();
      return;
    }
    Con_Printf("sv_giveall: no active server\n");
    return;
  }

  old_qcvm = qcvm;
  if (qcvm != &sv.qcvm) {
    if (qcvm)
      PR_SwitchQCVM(NULL);
    PR_SwitchQCVM(&sv.qcvm);
  }

  count = 0;
  if (Cmd_Argc() > 1 && q_strcasecmp(Cmd_Argv(1), "all") == 0) {
    for (i = 0; i < svs.maxclients; i++) {
      if (Host_GiveAllClient(&svs.clients[i])) {
        Con_Printf("sv_giveall: gave all weapons/ammo to %s\n",
                   svs.clients[i].name);
        count++;
      }
    }
  } else {
    client = Host_FindClientByCommandArgs(1);
    if (client) {
      if (Host_GiveAllClient(client)) {
        Con_Printf("sv_giveall: gave all weapons/ammo to %s\n",
                   client->name);
        count++;
      }
    }
  }

  if (!count)
    Con_Printf("sv_giveall: no matching active client\n");

  if (qcvm != old_qcvm) {
    PR_SwitchQCVM(NULL);
    if (old_qcvm)
      PR_SwitchQCVM(old_qcvm);
  }
}

static qboolean Host_ParseGiveKeysKind(const char *arg, int *key_flags) {
  if (!arg || !arg[0])
    return false;

  if (!q_strcasecmp(arg, "silver") || !q_strcasecmp(arg, "key1"))
    *key_flags = SV_COOP_GIVEKEYS_SILVER;
  else if (!q_strcasecmp(arg, "gold") || !q_strcasecmp(arg, "key2"))
    *key_flags = SV_COOP_GIVEKEYS_GOLD;
  else if (!q_strcasecmp(arg, "all") || !q_strcasecmp(arg, "both") ||
           !q_strcasecmp(arg, "keys"))
    *key_flags = SV_COOP_GIVEKEYS_ALL;
  else
    return false;

  return true;
}

static const char *Host_GiveKeysKindName(int key_flags) {
  if (key_flags == SV_COOP_GIVEKEYS_SILVER)
    return "silver key(s)";
  if (key_flags == SV_COOP_GIVEKEYS_GOLD)
    return "gold key(s)";
  return "all door keys";
}

static qboolean Host_GiveKeysClient(client_t *client, int key_flags) {
  client_t *old_host_client;
  edict_t *old_sv_player;
  qboolean given;

  if (!client || !client->active || !client->spawned || !client->edict ||
      client->edict->v.health <= 0)
    return false;

  old_host_client = host_client;
  old_sv_player = sv_player;
  host_client = client;
  sv_player = client->edict;

  given = SV_CoopGiveKeys(sv_player, key_flags);
  if (given)
    SV_CoopRespawnRefreshClientInventory(sv_player);

  host_client = old_host_client;
  sv_player = old_sv_player;
  return given;
}

/*
==================
Host_SV_GiveKeys_f

Server/admin key grant command. With no target, the requesting/first active
player is used, matching sv_giveall.
Usage: sv_givekeys [playername | # slot | all] [silver | gold | all]
==================
*/
static void Host_SV_GiveKeys_f(void) {
  client_t *client;
  qcvm_t *old_qcvm;
  int i, count;
  int key_flags = SV_COOP_GIVEKEYS_ALL;
  int kind_arg = 0;
  qboolean all_players = false;

  if (!sv.active) {
    if (cmd_source == src_command) {
      Cmd_ForwardToServer();
      return;
    }
    Con_Printf("sv_givekeys: no active server\n");
    return;
  }

  if (Cmd_Argc() > 1 && !q_strcasecmp(Cmd_Argv(1), "all")) {
    all_players = true;
    kind_arg = 2;
  } else {
    kind_arg = Cmd_Argc() > 1 && !Q_strcmp(Cmd_Argv(1), "#") ? 3 : 2;
  }
  if (Cmd_Argc() > kind_arg &&
      !Host_ParseGiveKeysKind(Cmd_Argv(kind_arg), &key_flags)) {
    Con_Printf("usage: sv_givekeys [playername | # slot | all] "
               "[silver | gold | all]\n");
    return;
  }

  old_qcvm = qcvm;
  if (qcvm != &sv.qcvm) {
    if (qcvm)
      PR_SwitchQCVM(NULL);
    PR_SwitchQCVM(&sv.qcvm);
  }

  count = 0;
  if (all_players) {
    for (i = 0; i < svs.maxclients; ++i) {
      if (!Host_GiveKeysClient(&svs.clients[i], key_flags))
        continue;
      Con_Printf("sv_givekeys: gave %s to %s\n",
                 Host_GiveKeysKindName(key_flags), svs.clients[i].name);
      count++;
    }
  } else {
    client = Host_FindClientByCommandArgs(1);
    if (Host_GiveKeysClient(client, key_flags)) {
      Con_Printf("sv_givekeys: gave %s to %s\n",
                 Host_GiveKeysKindName(key_flags), client->name);
      count++;
    }
  }

  if (!count)
    Con_Printf("sv_givekeys: no matching active client\n");

  if (qcvm != old_qcvm) {
    PR_SwitchQCVM(NULL);
    if (old_qcvm)
      PR_SwitchQCVM(old_qcvm);
  }
}

static qboolean Host_ParseServerCheatValue(int arg, qboolean default_value) {
  if (Cmd_Argc() <= arg)
    return default_value;

  return Q_atof(Cmd_Argv(arg)) != 0;
}

static void Host_SV_God_f(void) {
  client_t *client;
  int i;
  int value_arg;
  qboolean enable;
  qboolean all;

  if (!sv.active) {
    Con_Printf("sv_god: no active server\n");
    return;
  }

  all = Cmd_Argc() > 1 && q_strcasecmp(Cmd_Argv(1), "all") == 0;
  value_arg =
      (!all && Cmd_Argc() > 2 && Q_strcmp(Cmd_Argv(1), "#") == 0) ? 3 : 2;
  enable = Host_ParseServerCheatValue(value_arg, true);

  if (all) {
    for (i = 0; i < svs.maxclients; i++) {
      if (!svs.clients[i].active || !svs.clients[i].edict)
        continue;
      if (enable)
        svs.clients[i].edict->v.flags =
            (int)svs.clients[i].edict->v.flags | FL_GODMODE;
      else
        svs.clients[i].edict->v.flags =
            (int)svs.clients[i].edict->v.flags & ~FL_GODMODE;
      Con_Printf("sv_god: %s %s\n", svs.clients[i].name,
                 enable ? "ON" : "OFF");
    }
    return;
  }

  client = Host_FindClientByCommandArgs(1);
  if (!client || !client->edict) {
    Con_Printf("sv_god: no matching active client\n");
    return;
  }

  if (enable)
    client->edict->v.flags = (int)client->edict->v.flags | FL_GODMODE;
  else
    client->edict->v.flags = (int)client->edict->v.flags & ~FL_GODMODE;
  Con_Printf("sv_god: %s %s\n", client->name, enable ? "ON" : "OFF");
}

static void Host_SV_Noclip_f(void) {
  client_t *client;
  int i;
  int value_arg;
  qboolean enable;
  qboolean all;

  if (!sv.active) {
    Con_Printf("sv_noclip: no active server\n");
    return;
  }

  all = Cmd_Argc() > 1 && q_strcasecmp(Cmd_Argv(1), "all") == 0;
  value_arg =
      (!all && Cmd_Argc() > 2 && Q_strcmp(Cmd_Argv(1), "#") == 0) ? 3 : 2;
  enable = Host_ParseServerCheatValue(value_arg, true);

  if (all) {
    for (i = 0; i < svs.maxclients; i++) {
      if (!svs.clients[i].active || !svs.clients[i].edict)
        continue;
      svs.clients[i].edict->v.movetype =
          enable ? MOVETYPE_NOCLIP : MOVETYPE_WALK;
      Con_Printf("sv_noclip: %s %s\n", svs.clients[i].name,
                 enable ? "ON" : "OFF");
    }
    return;
  }

  client = Host_FindClientByCommandArgs(1);
  if (!client || !client->edict) {
    Con_Printf("sv_noclip: no matching active client\n");
    return;
  }

  client->edict->v.movetype = enable ? MOVETYPE_NOCLIP : MOVETYPE_WALK;
  Con_Printf("sv_noclip: %s %s\n", client->name, enable ? "ON" : "OFF");
}

static qboolean Host_SV_IsSafeGameName(const char *game) {
  const unsigned char *p;

  if (!game || !*game || !strcmp(game, ".") || strstr(game, ".."))
    return false;

  for (p = (const unsigned char *)game; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
      return false;
  }

  return true;
}

static qboolean Host_SV_IsSafeServerAddress(const char *server) {
  const unsigned char *p;

  if (!server || !*server)
    return false;

  for (p = (const unsigned char *)server; *p; p++) {
    if (*p <= 32 || *p == '"' || *p == '\'' || *p == '\\' || *p == ';')
      return false;
  }

  return true;
}

/*
==================
Host_SV_ReconnectGame_f

Tell clients to switch mod locally and reconnect with retry timing. The server
wrapper remains responsible for restarting the dedicated process.
==================
*/
static void Host_SV_ReconnectGame_f(void) {
  const char *game;
  const char *server;
  double delay;
  double retry_interval;
  double timeout;
  char command[512];
  byte data[1024];
  sizebuf_t msg;
  int failed;

  if (!sv.active) {
    Con_Printf("sv_reconnect_game: no active server\n");
    return;
  }

  if (Cmd_Argc() < 3) {
    Con_Printf("sv_reconnect_game <game> <server> [delay] [retry] [timeout]\n");
    return;
  }

  game = Cmd_Argv(1);
  server = Cmd_Argv(2);
  if (!Host_SV_IsSafeGameName(game)) {
    Con_Printf("sv_reconnect_game: invalid game directory \"%s\"\n", game);
    return;
  }
  if (!Host_SV_IsSafeServerAddress(server)) {
    Con_Printf("sv_reconnect_game: invalid server address \"%s\"\n", server);
    return;
  }

  delay = Cmd_Argc() > 3 ? Q_atof(Cmd_Argv(3)) : 8.0;
  retry_interval = Cmd_Argc() > 4 ? Q_atof(Cmd_Argv(4)) : 2.0;
  timeout = Cmd_Argc() > 5 ? Q_atof(Cmd_Argv(5)) : 120.0;
  delay = CLAMP(0.0, delay, 60.0);
  retry_interval = CLAMP(0.5, retry_interval, 15.0);
  timeout = CLAMP(delay + retry_interval, timeout, 300.0);

  q_snprintf(command, sizeof(command), "qs_reconnect_game \"%s\" \"%s\" %.3g %.3g %.3g\n",
             game, server, delay, retry_interval, timeout);

  msg.data = data;
  msg.cursize = 0;
  msg.maxsize = sizeof(data);
  MSG_WriteByte(&msg, svc_stufftext);
  MSG_WriteString(&msg, command);
  failed = NET_SendToAll(&msg, 5.0);

  Con_Printf("sv_reconnect_game: sent %s", command);
  if (failed)
    Con_Printf("sv_reconnect_game: failed to notify %d client(s)\n", failed);
}

static edict_t *FindViewthing(void) {
  int i;
  edict_t *e = NULL;

  PR_SwitchQCVM(&sv.qcvm);
  for (i = 0; i < qcvm->num_edicts; i++) {
    e = EDICT_NUM(i);
    if (!strcmp(PR_GetString(e->v.classname), "viewthing"))
      break;
  }
  if (i == qcvm->num_edicts) {
    e = NULL;
    Con_Printf("No viewthing on map\n");
  }
  PR_SwitchQCVM(NULL);
  return e;
}

/*
==================
Host_Viewmodel_f
==================
*/
static void Host_Viewmodel_f(void) {
  edict_t *e;
  qmodel_t *m;

  e = FindViewthing();
  if (!e)
    return;

  m = Mod_ForName(Cmd_Argv(1), false);
  if (!m) {
    Con_Printf("Can't load %s\n", Cmd_Argv(1));
    return;
  }

  PR_SwitchQCVM(&sv.qcvm);
  e->v.frame = 0;
  cl.model_precache[(int)e->v.modelindex] = m;
  PR_SwitchQCVM(NULL);
}

/*
==================
Host_Viewframe_f
==================
*/
static void Host_Viewframe_f(void) {
  edict_t *e;
  int f;
  qmodel_t *m;

  e = FindViewthing();
  if (!e)
    return;
  m = cl.model_precache[(int)e->v.modelindex];

  f = atoi(Cmd_Argv(1));
  if (f >= m->numframes)
    f = m->numframes - 1;

  e->v.frame = f;
}

static void PrintFrameName(qmodel_t *m, int frame) {
  aliashdr_t *hdr;
  maliasframedesc_t *pframedesc;

  hdr = (aliashdr_t *)Mod_Extradata(m);
  if (!hdr)
    return;
  pframedesc = &hdr->frames[frame];

  Con_Printf("frame %i: %s\n", frame, pframedesc->name);
}

/*
==================
Host_Viewnext_f
==================
*/
static void Host_Viewnext_f(void) {
  edict_t *e;
  qmodel_t *m;

  e = FindViewthing();
  if (!e)
    return;
  m = cl.model_precache[(int)e->v.modelindex];

  e->v.frame = e->v.frame + 1;
  if (e->v.frame >= m->numframes)
    e->v.frame = m->numframes - 1;

  PrintFrameName(m, e->v.frame);
}

/*
==================
Host_Viewprev_f
==================
*/
static void Host_Viewprev_f(void) {
  edict_t *e;
  qmodel_t *m;

  e = FindViewthing();
  if (!e)
    return;

  m = cl.model_precache[(int)e->v.modelindex];

  e->v.frame = e->v.frame - 1;
  if (e->v.frame < 0)
    e->v.frame = 0;

  PrintFrameName(m, e->v.frame);
}

/*
===============================================================================

DEMO LOOP CONTROL

===============================================================================
*/

/*
==================
Host_Startdemos_f
==================
*/
static qboolean Host_CommandLineTokenMatches(const char *arg,
                                             const char *command) {
  size_t len;

  if (!arg || arg[0] != '+')
    return false;

  len = strlen(command);
  if (q_strncasecmp(arg + 1, command, len))
    return false;

  return arg[1 + len] == 0 || arg[1 + len] == ' ' || arg[1 + len] == '\t';
}

static qboolean Host_CommandLineHasStartupCommand(void) {
  int i;

  for (i = 1; i < com_argc; i++) {
    if (Host_CommandLineTokenMatches(com_argv[i], "connect") ||
        Host_CommandLineTokenMatches(com_argv[i], "map") ||
        Host_CommandLineTokenMatches(com_argv[i], "load") ||
        Host_CommandLineTokenMatches(com_argv[i], "playdemo") ||
        Host_CommandLineTokenMatches(com_argv[i], "timedemo"))
      return true;
  }

  return false;
}

static qboolean Host_CommandLineDisablesStartdemos(void) {
  int i;
  const char *arg;
  const char *value;
  size_t len = strlen("cl_startdemos");

  for (i = 1; i < com_argc; i++) {
    arg = com_argv[i];
    if (!Host_CommandLineTokenMatches(arg, "cl_startdemos"))
      continue;

    value = arg + 1 + len;
    while (*value == ' ' || *value == '\t')
      value++;

    if (!*value && i + 1 < com_argc)
      value = com_argv[i + 1];
    if (value && *value && Q_atof(value) == 0.0f)
      return true;
  }

  return false;
}

static void Host_Startdemos_f(void) {
  int i, c;
  qboolean command_line_startup;

  if (cls.state == ca_dedicated || cls.state == ca_connected)
    return;

  command_line_startup = Host_CommandLineHasStartupCommand();
  if (Cmd_IsExecutingConfig() &&
      (command_line_startup || Host_CommandLineDisablesStartdemos())) {
    cls.demonum = -1;
    if (vr_enabled.value && !command_line_startup) {
      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("map start\n");
      Cbuf_AddText("centerview\n");
    }
    Con_DPrintf("Skipping startup demos because the command line controls startup.\n");
    return;
  }

  c = Cmd_Argc() - 1;
  if (c > MAX_DEMOS) {
    Con_Printf("Max %i demos in demoloop\n", MAX_DEMOS);
    c = MAX_DEMOS;
  }
  Con_Printf("%i demo(s) in loop\n", c);

  for (i = 1; i < c + 1; i++)
    q_strlcpy(cls.demos[i - 1], Cmd_Argv(i), sizeof(cls.demos[0]));

  if (!sv.active && cls.demonum != -1 && !cls.demoplayback) {
    cls.demonum = 0;
    if (vr_enabled.value) {
      // Start a new game when vr_enabled
      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n");
      Cbuf_AddText("coop 0\n");
      Cbuf_AddText("map start\n");
      Cbuf_AddText("centerview\n");
    }
    if (!fitzmode && !cl_startdemos.value) { /* QuakeSpasm customization: */
      /* go straight to menu, no CL_NextDemo */
      cls.demonum = -1;
      Cbuf_InsertText("menu_main\n");
      return;
    }
    CL_NextDemo();
  } else {
    cls.demonum = -1;
  }
}

/*
==================
Host_Demos_f

Return to looping demos
==================
*/
static void Host_Demos_f(void) {
  if (cls.state == ca_dedicated)
    return;
  if (!cl_startdemos.value && Cmd_IsExecutingConfig()) {
    cls.demonum = -1;
    Con_DPrintf("Skipping startup demos because cl_startdemos is 0.\n");
    return;
  }
  if (cls.demonum == -1)
    cls.demonum = 1;
  CL_Disconnect_f();
  CL_NextDemo();
}

/*
==================
Host_Stopdemo_f

Return to looping demos
==================
*/
static void Host_Stopdemo_f(void) {
  if (cls.state == ca_dedicated)
    return;
  if (!cls.demoplayback)
    return;
  CL_StopPlayback();
  CL_Disconnect();
}

/*
==================
Host_Resetdemos

Clear looping demo list (called on game change)
==================
*/
void Host_Resetdemos(void) {
  memset(cls.demos, 0, sizeof(cls.demos));
  cls.demonum = 0;
}

//=============================================================================

/*
==================
Host_InitCommands
==================
*/
void Host_InitCommands(void) {
  Cmd_AddCommand("maps", Host_Maps_f); // johnfitz
  Cmd_AddCommand("maps_mod", Host_Maps_Mod_f);
  Cmd_AddCommand("mods", Host_Mods_f); // johnfitz
  Cmd_AddCommand("games",
                 Host_Mods_f); // as an alias to "mods" -- S.A. / QuakeSpasm
  Cmd_AddCommand("mapname", Host_Mapname_f); // johnfitz
  Cmd_AddCommand("randmap", Host_Randmap_f); // ericw

  Cmd_AddCommand_ClientCommand("status", Host_Status_f);
  Cmd_AddCommand("quit", Host_Quit_f);
  Cmd_AddCommand_ClientCommand("god", Host_God_f);
  Cmd_AddCommand_ClientCommand("notarget", Host_Notarget_f);
  Cmd_AddCommand_ClientCommand("fly", Host_Fly_f);
  Cmd_AddCommand("map", Host_Map_f);
  Cmd_AddCommand("restart", Host_Restart_f);
  Cmd_AddCommand("changelevel", Host_Changelevel_f);
  Cmd_AddCommand("connect", Host_Connect_f);
  Cmd_AddCommand_Console("reconnect", Host_Reconnect_f);
  Cmd_AddCommand_ClientCommand("name", Host_Name_f);
  Cmd_AddCommand_ClientCommand("noclip", Host_Noclip_f);
  Cmd_AddCommand_ClientCommand("setpos", Host_SetPos_f); // QuakeSpasm

  Cmd_AddCommand_ClientCommand("say", Host_Say_f);
  Cmd_AddCommand_ClientCommand("say_team", Host_Say_Team_f);
  Cmd_AddCommand_ClientCommand("tell", Host_Tell_f);
  Cmd_AddCommand_ClientCommand("color", Host_Color_f);
  Cmd_AddCommand_ClientCommand("kill", Host_Kill_f);
  Cmd_AddCommand_ClientCommand("pause", Host_Pause_f);
  Cmd_AddCommand_ClientCommand("spawn", Host_Spawn_f);
  Cmd_AddCommand_ClientCommand("begin", Host_Begin_f);
  Cmd_AddCommand_ClientCommand("prespawn", Host_PreSpawn_f);
  Cmd_AddCommand_ClientCommand("coop_teleport_player", Host_CoopTeleportPlayer_f);
  Cmd_AddCommand_ClientCommand("coop_teleport_spawn", Host_CoopTeleportSpawn_f);
  Cmd_AddCommand_ClientCommand("enablecsqc", Host_EnableCSQC_f);
  Cmd_AddCommand_ClientCommand("disablecsqc", Host_DisableCSQC_f);
  Cmd_AddCommand("kick", Host_Kick_f);
  Cmd_AddCommand_ClientCommand("ping", Host_Ping_f);
  Cmd_AddCommand("load", Host_Loadgame_f);
  Cmd_AddCommand("save", Host_Savegame_f);
  Cmd_AddCommand_ClientCommand("give", Host_Give_f);
  Cmd_AddCommand("sv_giveall", Host_SV_GiveAll_f);
  Cmd_AddCommand("sv_givekeys", Host_SV_GiveKeys_f);
  Cmd_AddCommand("sv_god", Host_SV_God_f);
  Cmd_AddCommand("sv_noclip", Host_SV_Noclip_f);
  Cmd_AddCommand("sv_reconnect_game", Host_SV_ReconnectGame_f);

  Cmd_AddCommand("startdemos", Host_Startdemos_f);
  Cmd_AddCommand("demos", Host_Demos_f);
  Cmd_AddCommand("stopdemo", Host_Stopdemo_f);

  Cmd_AddCommand("viewmodel", Host_Viewmodel_f);
  Cmd_AddCommand("viewframe", Host_Viewframe_f);
  Cmd_AddCommand("viewnext", Host_Viewnext_f);
  Cmd_AddCommand("viewprev", Host_Viewprev_f);
}
