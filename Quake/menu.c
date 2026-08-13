/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
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

// clang-format off
#include "quakedef.h"
#include "addon_catalog.h"
#include "bgmusic.h"
#include "vr_menu.h"
// clang-format on

void (*vid_menucmdfn)(void); // johnfitz
void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);
qboolean (*vid_menupointerfn)(float x, float y);

void (*vr_menucmdfn)(void);
void (*vr_menudrawfn)(void);
void (*vr_menukeyfn)(int key);

enum m_state_e m_state;

void M_Menu_Main_f(void);
void M_Menu_SinglePlayer_f(void);
void M_Menu_Load_f(void);
void M_Menu_Save_f(void);
void M_Menu_MultiPlayer_f(void);
void M_Menu_Setup_f(void);
void M_Menu_Net_f(void);
void M_Menu_LanConfig_f(void);
void M_Menu_GameOptions_f(void);
void M_Menu_Search_f(void);
void M_Menu_ServerList_f(void);
void M_Menu_Options_f(void);
void M_Menu_Keys_f(void);
static void M_Menu_Weapons_f(void);
void M_Menu_Video_f(void);
void M_Menu_VR_f(void);
void M_Menu_Help_f(void);
static void M_Menu_Mods_f(void);
void M_Menu_ServerModDownload_f(void);
static void M_Menu_Models_f(void);
void M_Menu_Quit_f(void);

void M_Main_Draw(void);
void M_SinglePlayer_Draw(void);
void M_Load_Draw(void);
void M_Save_Draw(void);
void M_MultiPlayer_Draw(void);
void M_Setup_Draw(void);
void M_Net_Draw(void);
void M_LanConfig_Draw(void);
void M_GameOptions_Draw(void);
void M_Search_Draw(void);
void M_ServerList_Draw(void);
void M_Options_Draw(void);
void M_Keys_Draw(void);
static void M_Weapons_Draw(void);
void M_Video_Draw(void);
void M_VR_Draw(void);
void M_Help_Draw(void);
static void M_Mods_Draw(void);
static void M_ServerModDownload_Draw(void);
static void M_Models_Draw(void);
void M_Quit_Draw(void);

void M_Main_Key(int key);
void M_SinglePlayer_Key(int key);
void M_Load_Key(int key);
void M_Save_Key(int key);
void M_MultiPlayer_Key(int key);
void M_Setup_Key(int key);
void M_Net_Key(int key);
void M_LanConfig_Key(int key);
void M_GameOptions_Key(int key);
void M_Search_Key(int key);
void M_ServerList_Key(int key);
void M_Options_Key(int key);
void M_Keys_Key(int key);
static void M_Weapons_Key(int key);
void M_Video_Key(int key);
void M_VR_Key(int key);
void M_Help_Key(int key);
static void M_Mods_Key(int key);
static void M_ServerModDownload_Key(int key);
static void M_Models_Key(int key);
static void M_Mods_Char(int key);
void M_Quit_Key(int key);

qboolean m_entersound; // play after drawing a frame, so caching
                       // won't disrupt the sound
qboolean m_recursiveDraw;

/* Shared hover state for desktop and VR pointer adapters. */
static qboolean m_pointer_hover;
static m_pointer_source_t m_pointer_source;
static enum m_state_e m_pointer_state;
static int m_pointer_activate_key = K_ENTER;

enum m_state_e m_return_state;
qboolean m_return_onerror;
char m_return_reason[32];

#define StartingGame (m_multiplayer_cursor == 1)
#define JoiningGame (m_multiplayer_cursor == 0)
#define IPXConfig (m_net_cursor == 0)
#define TCPIPConfig (m_net_cursor == 1)

void M_ConfigureNetSubsystem(void);

/*
================
M_DrawCharacter

Draws one solid graphics character
================
*/
void M_DrawCharacter(int cx, int line, int num) {
  Draw_Character(cx, line, num);
}

void M_Print(int cx, int cy, const char *str) {
  while (*str) {
    M_DrawCharacter(cx, cy, (*str) + 128);
    str++;
    cx += 8;
  }
}

void M_PrintWhite(int cx, int cy, const char *str) {
  while (*str) {
    M_DrawCharacter(cx, cy, *str);
    str++;
    cx += 8;
  }
}

void M_DrawTransPic(int x, int y, qpic_t *pic) {
  Draw_Pic(
      x, y,
      pic); // johnfitz -- simplified becuase centering is handled elsewhere
}

void M_DrawPic(int x, int y, qpic_t *pic) {
  Draw_Pic(
      x, y,
      pic); // johnfitz -- simplified becuase centering is handled elsewhere
}

static void M_DrawSubpic(int x, int y, qpic_t *pic, int left, int top,
                         int width, int height) {
  Draw_SubPic((float)x, (float)y, (float)width, (float)height, pic,
              (float)left / pic->width, (float)top / pic->height,
              (float)width / pic->width, (float)height / pic->height, NULL,
              1.0f);
}

void M_DrawTransPicTranslate(int x, int y, qpic_t *pic, int top,
                             int bottom) // johnfitz -- more parameters
{
  Draw_TransPicTranslate(
      x, y, pic, top,
      bottom); // johnfitz -- simplified becuase centering is handled elsewhere
}

void M_DrawTextBox(int x, int y, int width, int lines) {
  qpic_t *p;
  int cx, cy;
  int n;

  // draw left side
  cx = x;
  cy = y;
  p = Draw_CachePic("gfx/box_tl.lmp");
  M_DrawTransPic(cx, cy, p);
  p = Draw_CachePic("gfx/box_ml.lmp");
  for (n = 0; n < lines; n++) {
    cy += 8;
    M_DrawTransPic(cx, cy, p);
  }
  p = Draw_CachePic("gfx/box_bl.lmp");
  M_DrawTransPic(cx, cy + 8, p);

  // draw middle
  cx += 8;
  while (width > 0) {
    cy = y;
    p = Draw_CachePic("gfx/box_tm.lmp");
    M_DrawTransPic(cx, cy, p);
    p = Draw_CachePic("gfx/box_mm.lmp");
    for (n = 0; n < lines; n++) {
      cy += 8;
      if (n == 1)
        p = Draw_CachePic("gfx/box_mm2.lmp");
      M_DrawTransPic(cx, cy, p);
    }
    p = Draw_CachePic("gfx/box_bm.lmp");
    M_DrawTransPic(cx, cy + 8, p);
    width -= 2;
    cx += 16;
  }

  // draw right side
  cy = y;
  p = Draw_CachePic("gfx/box_tr.lmp");
  M_DrawTransPic(cx, cy, p);
  p = Draw_CachePic("gfx/box_mr.lmp");
  for (n = 0; n < lines; n++) {
    cy += 8;
    M_DrawTransPic(cx, cy, p);
  }
  p = Draw_CachePic("gfx/box_br.lmp");
  M_DrawTransPic(cx, cy + 8, p);
}

//=============================================================================

int m_save_demonum;

/*
================
M_ToggleMenu_f
================
*/
void M_ToggleMenu_f(void) {
  m_entersound = true;

  if (key_dest == key_menu) {
    if (m_state != m_main) {
      M_Menu_Main_f();
      return;
    }

    IN_Activate();
    key_dest = key_game;
    m_state = m_none;
    return;
  }
  if (key_dest == key_console) {
    Con_ToggleConsole_f();
  } else {
    M_Menu_Main_f();
  }
}

//=============================================================================
/* MAIN MENU */

int m_main_cursor;
enum {
  MAIN_SINGLEPLAYER,
  MAIN_MULTIPLAYER,
  MAIN_OPTIONS,
  MAIN_MODS,
  MAIN_HELP,
  MAIN_QUIT,

  MAIN_ITEMS
};

void M_Menu_Main_f(void) {
  /* Leaving the server add-on modal is also an explicit rejection/cancel. */
  if (m_state == m_servermod)
    CL_ServerModDownload_Cancel();
  if (key_dest != key_menu) {
    m_save_demonum = cls.demonum;
    cls.demonum = -1;
  }
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_main;
  m_entersound = true;
}

void M_Main_Draw(void) {
  int f;
  qpic_t *p;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  p = Draw_CachePic("gfx/ttl_main.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);
  p = Draw_CachePic("gfx/mainmenu.lmp");

  /* Match Ironwail: insert Mods before the baked Help/Quit artwork. */
  M_DrawSubpic(72, 32, p, 0, 0, p->width, 60);
  M_DrawTransPic(72, 92, Draw_CachePic("gfx/menumods.lmp"));
  M_DrawSubpic(72, 112, p, 0, 60, p->width, p->height - 60);

  f = (int)(realtime * 10) % 6;

  M_DrawTransPic(54, 32 + m_main_cursor * 20,
                 Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_Main_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    IN_Activate();
    key_dest = key_game;
    m_state = m_none;
    cls.demonum = m_save_demonum;
    if (!fitzmode && !cl_startdemos.value) /* QuakeSpasm customization: */
      break;
    if (cls.demonum != -1 && !cls.demoplayback && cls.state != ca_connected)
      CL_NextDemo();
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    if (++m_main_cursor >= MAIN_ITEMS)
      m_main_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    if (--m_main_cursor < 0)
      m_main_cursor = MAIN_ITEMS - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;

    switch (m_main_cursor) {
    case MAIN_SINGLEPLAYER:
      M_Menu_SinglePlayer_f();
      break;

    case MAIN_MULTIPLAYER:
      M_Menu_MultiPlayer_f();
      break;

    case MAIN_OPTIONS:
      M_Menu_Options_f();
      break;

    case MAIN_MODS:
      M_Menu_Mods_f();
      break;

    case MAIN_HELP:
      M_Menu_Help_f();
      break;

    case MAIN_QUIT:
      M_Menu_Quit_f();
      break;
    }
  }
}

//=============================================================================
/* SINGLE PLAYER MENU */

int m_singleplayer_cursor;
#define SINGLEPLAYER_ITEMS 3

void M_Menu_SinglePlayer_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_singleplayer;
  m_entersound = true;
}

void M_SinglePlayer_Draw(void) {
  int f;
  qpic_t *p;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  p = Draw_CachePic("gfx/ttl_sgl.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);
  M_DrawTransPic(72, 32, Draw_CachePic("gfx/sp_menu.lmp"));

  f = (int)(realtime * 10) % 6;

  M_DrawTransPic(54, 32 + m_singleplayer_cursor * 20,
                 Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_SinglePlayer_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    if (++m_singleplayer_cursor >= SINGLEPLAYER_ITEMS)
      m_singleplayer_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    if (--m_singleplayer_cursor < 0)
      m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;

    switch (m_singleplayer_cursor) {
    case 0:
      IN_Activate();
      key_dest = key_game;
      if (sv.active)
        Cbuf_AddText("disconnect\n");
      Cbuf_AddText("maxplayers 1\n");
      Cbuf_AddText("deathmatch 0\n"); // johnfitz
      Cbuf_AddText("coop 0\n");       // johnfitz
      Cbuf_AddText("map start\n");
      break;

    case 1:
      M_Menu_Load_f();
      break;

    case 2:
      M_Menu_Save_f();
      break;
    }
  }
}

//=============================================================================
/* LOAD/SAVE MENU */

int load_cursor; // 0 < load_cursor < MAX_SAVEGAMES

#define MAX_SAVEGAMES 20 /* johnfitz -- increased from 12 */
char m_filenames[MAX_SAVEGAMES][SAVEGAME_COMMENT_LENGTH + 1];
int loadable[MAX_SAVEGAMES];

void M_ScanSaves(void) {
  int i, j;
  char name[MAX_OSPATH];
  FILE *f;
  int version;

  for (i = 0; i < MAX_SAVEGAMES; i++) {
    strcpy(m_filenames[i], "--- UNUSED SLOT ---");
    loadable[i] = false;
    q_snprintf(name, sizeof(name), "%s/s%i.sav", com_gamedir, i);
    f = fopen(name, "r");
    if (!f) {
      continue;
    }
    if (fscanf(f, "%i\n", &version) != 1 || fscanf(f, "%79s\n", name) != 1) {
      fclose(f);
      continue;
    }
    q_strlcpy(m_filenames[i], name, SAVEGAME_COMMENT_LENGTH + 1);

    // change _ back to space
    for (j = 0; j < SAVEGAME_COMMENT_LENGTH; j++) {
      if (m_filenames[i][j] == '_')
        m_filenames[i][j] = ' ';
    }
    loadable[i] = true;
    fclose(f);
  }
}

void M_Menu_Load_f(void) {
  m_entersound = true;
  m_state = m_load;

  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  M_ScanSaves();
}

void M_Menu_Save_f(void) {
  if (!sv.active)
    return;
  if (cl.intermission)
    return;
  if (svs.maxclients != 1)
    return;
  m_entersound = true;
  m_state = m_save;

  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  M_ScanSaves();
}

void M_Load_Draw(void) {
  int i;
  qpic_t *p;

  p = Draw_CachePic("gfx/p_load.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);

  for (i = 0; i < MAX_SAVEGAMES; i++)
    M_Print(16, 32 + 8 * i, m_filenames[i]);

  // line cursor
  M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Save_Draw(void) {
  int i;
  qpic_t *p;

  p = Draw_CachePic("gfx/p_save.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);

  for (i = 0; i < MAX_SAVEGAMES; i++)
    M_Print(16, 32 + 8 * i, m_filenames[i]);

  // line cursor
  M_DrawCharacter(8, 32 + load_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Load_Key(int k) {
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_SinglePlayer_f();
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    S_LocalSound("misc/menu2.wav");
    if (!loadable[load_cursor])
      return;
    m_state = m_none;
    key_dest = key_game;

    // Host_Loadgame_f can't bring up the loading plaque because too much
    // stack space has been used, so do it now
    SCR_BeginLoadingPlaque();

    // issue the load command
    Cbuf_AddText(va("load s%i\n", load_cursor));
    return;

  case K_UPARROW:
  case K_LEFTARROW:
    S_LocalSound("misc/menu1.wav");
    load_cursor--;
    if (load_cursor < 0)
      load_cursor = MAX_SAVEGAMES - 1;
    break;

  case K_DOWNARROW:
  case K_RIGHTARROW:
    S_LocalSound("misc/menu1.wav");
    load_cursor++;
    if (load_cursor >= MAX_SAVEGAMES)
      load_cursor = 0;
    break;
  }
}

void M_Save_Key(int k) {
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_SinglePlayer_f();
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_state = m_none;
    IN_Activate();
    key_dest = key_game;
    Cbuf_AddText(va("save s%i\n", load_cursor));
    return;

  case K_UPARROW:
  case K_LEFTARROW:
    S_LocalSound("misc/menu1.wav");
    load_cursor--;
    if (load_cursor < 0)
      load_cursor = MAX_SAVEGAMES - 1;
    break;

  case K_DOWNARROW:
  case K_RIGHTARROW:
    S_LocalSound("misc/menu1.wav");
    load_cursor++;
    if (load_cursor >= MAX_SAVEGAMES)
      load_cursor = 0;
    break;
  }
}

//=============================================================================
/* MULTIPLAYER MENU */

int m_multiplayer_cursor;
#define MULTIPLAYER_ITEMS 3

void M_Menu_MultiPlayer_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_multiplayer;
  m_entersound = true;
}

void M_MultiPlayer_Draw(void) {
  int f;
  qpic_t *p;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  p = Draw_CachePic("gfx/p_multi.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);
  M_DrawTransPic(72, 32, Draw_CachePic("gfx/mp_menu.lmp"));

  f = (int)(realtime * 10) % 6;

  M_DrawTransPic(54, 32 + m_multiplayer_cursor * 20,
                 Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));

  if (ipxAvailable || tcpipAvailable)
    return;
  M_PrintWhite((320 / 2) - ((27 * 8) / 2), 148, "No Communications Available");
}

void M_MultiPlayer_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    if (++m_multiplayer_cursor >= MULTIPLAYER_ITEMS)
      m_multiplayer_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    if (--m_multiplayer_cursor < 0)
      m_multiplayer_cursor = MULTIPLAYER_ITEMS - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;
    switch (m_multiplayer_cursor) {
    case 0:
      if (ipxAvailable || tcpipAvailable)
        M_Menu_Net_f();
      break;

    case 1:
      if (ipxAvailable || tcpipAvailable)
        M_Menu_Net_f();
      break;

    case 2:
      M_Menu_Setup_f();
      break;
    }
  }
}

//=============================================================================
/* SETUP MENU */

int setup_cursor = 4;
int setup_cursor_table[] = {40, 56, 80, 104, 140};

char setup_hostname[16];
char setup_myname[16];
int setup_oldtop;
int setup_oldbottom;
int setup_top;
int setup_bottom;

#define NUM_SETUP_CMDS 5

void M_Menu_Setup_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_setup;
  m_entersound = true;
  Q_strcpy(setup_myname, cl_name.string);
  Q_strcpy(setup_hostname, hostname.string);
  setup_top = setup_oldtop = ((int)cl_color.value) >> 4;
  setup_bottom = setup_oldbottom = ((int)cl_color.value) & 15;
}

void M_Setup_Draw(void) {
  qpic_t *p;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  p = Draw_CachePic("gfx/p_multi.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);

  M_Print(64, 40, "Hostname");
  M_DrawTextBox(160, 32, 16, 1);
  M_Print(168, 40, setup_hostname);

  M_Print(64, 56, "Your name");
  M_DrawTextBox(160, 48, 16, 1);
  M_Print(168, 56, setup_myname);

  M_Print(64, 80, "Shirt color");
  M_Print(64, 104, "Pants color");

  M_DrawTextBox(64, 140 - 8, 14, 1);
  M_Print(72, 140, "Accept Changes");

  p = Draw_CachePic("gfx/bigbox.lmp");
  M_DrawTransPic(160, 64, p);
  p = Draw_CachePic("gfx/menuplyr.lmp");
  M_DrawTransPicTranslate(172, 72, p, setup_top, setup_bottom);

  M_DrawCharacter(56, setup_cursor_table[setup_cursor],
                  12 + ((int)(realtime * 4) & 1));

  if (setup_cursor == 0)
    M_DrawCharacter(168 + 8 * strlen(setup_hostname),
                    setup_cursor_table[setup_cursor],
                    10 + ((int)(realtime * 4) & 1));

  if (setup_cursor == 1)
    M_DrawCharacter(168 + 8 * strlen(setup_myname),
                    setup_cursor_table[setup_cursor],
                    10 + ((int)(realtime * 4) & 1));
}

void M_Setup_Key(int k) {
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_MultiPlayer_f();
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    setup_cursor--;
    if (setup_cursor < 0)
      setup_cursor = NUM_SETUP_CMDS - 1;
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    setup_cursor++;
    if (setup_cursor >= NUM_SETUP_CMDS)
      setup_cursor = 0;
    break;

  case K_LEFTARROW:
    if (setup_cursor < 2)
      return;
    S_LocalSound("misc/menu3.wav");
    if (setup_cursor == 2)
      setup_top = setup_top - 1;
    if (setup_cursor == 3)
      setup_bottom = setup_bottom - 1;
    break;
  case K_RIGHTARROW:
    if (setup_cursor < 2)
      return;
  forward:
    S_LocalSound("misc/menu3.wav");
    if (setup_cursor == 2)
      setup_top = setup_top + 1;
    if (setup_cursor == 3)
      setup_bottom = setup_bottom + 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    if (setup_cursor == 0 || setup_cursor == 1)
      return;

    if (setup_cursor == 2 || setup_cursor == 3)
      goto forward;

    // setup_cursor == 4 (OK)
    if (Q_strcmp(cl_name.string, setup_myname) != 0)
      Cbuf_AddText(va("name \"%s\"\n", setup_myname));
    if (Q_strcmp(hostname.string, setup_hostname) != 0)
      Cvar_Set("hostname", setup_hostname);
    if (setup_top != setup_oldtop || setup_bottom != setup_oldbottom)
      Cbuf_AddText(va("color %i %i\n", setup_top, setup_bottom));
    m_entersound = true;
    M_Menu_MultiPlayer_f();
    break;

  case K_BACKSPACE:
    if (setup_cursor == 0) {
      if (strlen(setup_hostname))
        setup_hostname[strlen(setup_hostname) - 1] = 0;
    }

    if (setup_cursor == 1) {
      if (strlen(setup_myname))
        setup_myname[strlen(setup_myname) - 1] = 0;
    }
    break;
  }

  if (setup_top > 13)
    setup_top = 0;
  if (setup_top < 0)
    setup_top = 13;
  if (setup_bottom > 13)
    setup_bottom = 0;
  if (setup_bottom < 0)
    setup_bottom = 13;
}

void M_Setup_Char(int k) {
  int l;

  switch (setup_cursor) {
  case 0:
    l = strlen(setup_hostname);
    if (l < 15) {
      setup_hostname[l + 1] = 0;
      setup_hostname[l] = k;
    }
    break;
  case 1:
    l = strlen(setup_myname);
    if (l < 15) {
      setup_myname[l + 1] = 0;
      setup_myname[l] = k;
    }
    break;
  }
}

qboolean M_Setup_TextEntry(void) {
  return (setup_cursor == 0 || setup_cursor == 1);
}

//=============================================================================
/* NET MENU */

int m_net_cursor;
int m_net_items;

const char *net_helpMessage[] = {
    /* .........1.........2.... */
    " Novell network LANs    ", " or Windows 95 DOS-box. ",
    "                        ", "(LAN=Local Area Network)",

    " Commonly used to play  ", " over the Internet, but ",
    " also used on a Local   ", " Area Network.          "};

void M_Menu_Net_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_net;
  m_entersound = true;
  m_net_items = 2;

  if (m_net_cursor >= m_net_items)
    m_net_cursor = 0;
  m_net_cursor--;
  M_Net_Key(K_DOWNARROW);
}

void M_Net_Draw(void) {
  int f;
  qpic_t *p;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  p = Draw_CachePic("gfx/p_multi.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);

  f = 32;

  if (ipxAvailable)
    p = Draw_CachePic("gfx/netmen3.lmp");
  else
    p = Draw_CachePic("gfx/dim_ipx.lmp");
  M_DrawTransPic(72, f, p);

  f += 19;
  if (tcpipAvailable)
    p = Draw_CachePic("gfx/netmen4.lmp");
  else
    p = Draw_CachePic("gfx/dim_tcp.lmp");
  M_DrawTransPic(72, f, p);

  f = (320 - 26 * 8) / 2;
  M_DrawTextBox(f, 96, 24, 4);
  f += 8;
  M_Print(f, 104, net_helpMessage[m_net_cursor * 4 + 0]);
  M_Print(f, 112, net_helpMessage[m_net_cursor * 4 + 1]);
  M_Print(f, 120, net_helpMessage[m_net_cursor * 4 + 2]);
  M_Print(f, 128, net_helpMessage[m_net_cursor * 4 + 3]);

  f = (int)(realtime * 10) % 6;
  M_DrawTransPic(54, 32 + m_net_cursor * 20,
                 Draw_CachePic(va("gfx/menudot%i.lmp", f + 1)));
}

void M_Net_Key(int k) {
again:
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_MultiPlayer_f();
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    if (++m_net_cursor >= m_net_items)
      m_net_cursor = 0;
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    if (--m_net_cursor < 0)
      m_net_cursor = m_net_items - 1;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;
    M_Menu_LanConfig_f();
    break;
  }

  if (m_net_cursor == 0 && !ipxAvailable)
    goto again;
  if (m_net_cursor == 1 && !tcpipAvailable)
    goto again;
}

//=============================================================================
/* OPTIONS MENU */

enum {
  OPT_CUSTOMIZE = 0,
  OPT_WEAPONS,
  OPT_CONSOLE,  // 2
  OPT_DEFAULTS, // 3
  OPT_SCALE,
  OPT_SCRSIZE,
  OPT_GAMMA,
  OPT_CONTRAST,
  OPT_MOUSESPEED,
  OPT_SBALPHA,
  OPT_SNDVOL,
  OPT_MUSICVOL,
  OPT_MUSICEXT,
  OPT_ALWAYRUN,
  OPT_INVMOUSE,
  OPT_ALWAYSMLOOK,
  OPT_LOOKSPRING,
  OPT_LOOKSTRAFE,
  // #ifdef _WIN32
  //	OPT_USEMOUSE,
  // #endif
  OPT_VIDEO, // This is the last before OPTIONS_ITEMS
  OPT_VR,
  OPT_MODELS,
  OPTIONS_ITEMS
};

enum {
  ALWAYSRUN_OFF = 0,
  ALWAYSRUN_VANILLA,
  ALWAYSRUN_QUAKESPASM,
  ALWAYSRUN_ITEMS
};

#define SLIDER_RANGE 10

int options_cursor;

void M_Menu_Options_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_options;
  m_entersound = true;
}

void M_AdjustSliders(int dir) {
  int curr_alwaysrun, target_alwaysrun;
  float f, l;

  S_LocalSound("misc/menu3.wav");

  switch (options_cursor) {
  case OPT_SCALE: // console and menu scale
    l = ((vid.width + 31) / 32) / 10.0;
    f = scr_conscale.value + dir * .1;
    if (f < 1)
      f = 1;
    else if (f > l)
      f = l;
    Cvar_SetValue("scr_autoscale", 0);
    Cvar_SetValue("scr_conscale", f);
    Cvar_SetValue("scr_menuscale", f);
    Cvar_SetValue("scr_sbarscale", f);
    break;
  case OPT_SCRSIZE: // screen size
    f = scr_viewsize.value + dir * 10;
    if (f > 120)
      f = 120;
    else if (f < 30)
      f = 30;
    Cvar_SetValue("viewsize", f);
    break;
  case OPT_GAMMA: // gamma
    f = vid_gamma.value - dir * 0.05;
    if (f < 0.5)
      f = 0.5;
    else if (f > 2)
      f = 2;
    Cvar_SetValue("gamma", f);
    break;
  case OPT_CONTRAST: // contrast
    f = vid_contrast.value + dir * 0.1;
    if (f < 1)
      f = 1;
    else if (f > 2)
      f = 2;
    Cvar_SetValue("contrast", f);
    break;
  case OPT_MOUSESPEED: // mouse speed
    f = sensitivity.value + dir * 0.5;
    if (f > 11)
      f = 11;
    else if (f < 1)
      f = 1;
    Cvar_SetValue("sensitivity", f);
    break;
  case OPT_SBALPHA: // statusbar alpha
    f = scr_sbaralpha.value - dir * 0.05;
    if (f < 0)
      f = 0;
    else if (f > 1)
      f = 1;
    Cvar_SetValue("scr_sbaralpha", f);
    break;
  case OPT_MUSICVOL: // music volume
    f = bgmvolume.value + dir * 0.1;
    if (f < 0)
      f = 0;
    else if (f > 1)
      f = 1;
    Cvar_SetValue("bgmvolume", f);
    break;
  case OPT_MUSICEXT: // enable external music vs cdaudio
    Cvar_Set("bgm_extmusic", bgm_extmusic.value ? "0" : "1");
    break;
  case OPT_SNDVOL: // sfx volume
    f = sfxvolume.value + dir * 0.1;
    if (f < 0)
      f = 0;
    else if (f > 1)
      f = 1;
    Cvar_SetValue("volume", f);
    break;

  case OPT_ALWAYRUN: // always run
    if (cl_alwaysrun.value)
      curr_alwaysrun = ALWAYSRUN_QUAKESPASM;
    else if (cl_forwardspeed.value > 200)
      curr_alwaysrun = ALWAYSRUN_VANILLA;
    else
      curr_alwaysrun = ALWAYSRUN_OFF;

    target_alwaysrun =
        (ALWAYSRUN_ITEMS + curr_alwaysrun + dir) % ALWAYSRUN_ITEMS;

    if (target_alwaysrun == ALWAYSRUN_VANILLA) {
      Cvar_SetValue("cl_alwaysrun", 0);
      Cvar_SetValue("cl_forwardspeed", 400);
      Cvar_SetValue("cl_backspeed", 400);
    } else if (target_alwaysrun == ALWAYSRUN_QUAKESPASM) {
      Cvar_SetValue("cl_alwaysrun", 1);
      Cvar_SetValue("cl_forwardspeed", 200);
      Cvar_SetValue("cl_backspeed", 200);
    } else // ALWAYSRUN_OFF
    {
      Cvar_SetValue("cl_alwaysrun", 0);
      Cvar_SetValue("cl_forwardspeed", 200);
      Cvar_SetValue("cl_backspeed", 200);
    }
    break;

  case OPT_INVMOUSE: // invert mouse
    Cvar_SetValue("m_pitch", -m_pitch.value);
    break;

  case OPT_ALWAYSMLOOK:
    if (in_mlook.state & 1)
      Cbuf_AddText("-mlook");
    else
      Cbuf_AddText("+mlook");
    break;

  case OPT_LOOKSPRING: // lookspring
    Cvar_Set("lookspring", lookspring.value ? "0" : "1");
    break;

  case OPT_LOOKSTRAFE: // lookstrafe
    Cvar_Set("lookstrafe", lookstrafe.value ? "0" : "1");
    break;
  }
}

void M_DrawSlider(int x, int y, float range, float value, const char *format) {
  int i;
  char buffer[5];

  if (range < 0)
    range = 0;
  if (range > 1)
    range = 1;
  M_DrawCharacter(x - 8, y, 128);
  for (i = 0; i < SLIDER_RANGE; i++)
    M_DrawCharacter(x + i * 8, y, 129);
  M_DrawCharacter(x + i * 8, y, 130);
  M_DrawCharacter(x + (SLIDER_RANGE - 1) * 8 * range, y, 131);
  q_snprintf(buffer, 5, format, value);
  M_Print(x + (SLIDER_RANGE + 1) * 8, y, buffer);
}

void M_DrawCheckbox(int x, int y, int on) {
#if 0
	if (on)
		M_DrawCharacter (x, y, 131);
	else
		M_DrawCharacter (x, y, 129);
#endif
  if (on)
    M_Print(x, y, "on");
  else
    M_Print(x, y, "off");
}

void M_Options_Draw(void) {
  float r, l;
  qpic_t *p;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  p = Draw_CachePic("gfx/p_option.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);

  // Draw the items in the order of the enum defined above:
  // OPT_CUSTOMIZE:
  M_Print(16, 32, "              Controls");
  // OPT_WEAPONS:
  M_Print(16, 32 + 8 * OPT_WEAPONS, "        Weapon bindings");
  // OPT_CONSOLE:
  M_Print(16, 32 + 8 * OPT_CONSOLE, "          Goto console");
  // OPT_DEFAULTS:
  M_Print(16, 32 + 8 * OPT_DEFAULTS, "          Reset config");

  // OPT_SCALE:
  M_Print(16, 32 + 8 * OPT_SCALE, "                 Scale");
  l = (vid.width / 320.0) - 1;
  r = l > 0 ? (scr_conscale.value - 1) / l : 0;
  M_DrawSlider(220, 32 + 8 * OPT_SCALE, r, scr_conscale.value, "%.1f");

  // OPT_SCRSIZE:
  M_Print(16, 32 + 8 * OPT_SCRSIZE, "           Screen size");
  r = (scr_viewsize.value - 30) / (120 - 30);
  M_DrawSlider(220, 32 + 8 * OPT_SCRSIZE, r, scr_viewsize.value, "%.0f");

  // OPT_GAMMA:
  M_Print(16, 32 + 8 * OPT_GAMMA, "            Brightness");
  r = (2.0 - vid_gamma.value) / 1.5;
  M_DrawSlider(220, 32 + 8 * OPT_GAMMA, r, vid_gamma.value, "%.2f");

  // OPT_CONTRAST:
  M_Print(16, 32 + 8 * OPT_CONTRAST, "              Contrast");
  r = vid_contrast.value - 1.0;
  M_DrawSlider(220, 32 + 8 * OPT_CONTRAST, r, vid_contrast.value, "%.1f");

  // OPT_MOUSESPEED:
  M_Print(16, 32 + 8 * OPT_MOUSESPEED, "           Mouse Speed");
  r = (sensitivity.value - 1) / 10;
  M_DrawSlider(220, 32 + 8 * OPT_MOUSESPEED, r, sensitivity.value, "%.1f");

  // OPT_SBALPHA:
  M_Print(16, 32 + 8 * OPT_SBALPHA, "       Statusbar alpha");
  r = (1.0 - scr_sbaralpha.value); // scr_sbaralpha range is 1.0 to 0.0
  M_DrawSlider(220, 32 + 8 * OPT_SBALPHA, r, scr_sbaralpha.value, "%.2f");

  // OPT_SNDVOL:
  M_Print(16, 32 + 8 * OPT_SNDVOL, "          Sound Volume");
  r = sfxvolume.value;
  M_DrawSlider(220, 32 + 8 * OPT_SNDVOL, r, sfxvolume.value, "%.1f");

  // OPT_MUSICVOL:
  M_Print(16, 32 + 8 * OPT_MUSICVOL, "          Music Volume");
  r = bgmvolume.value;
  M_DrawSlider(220, 32 + 8 * OPT_MUSICVOL, r, bgmvolume.value, "%.1f");

  // OPT_MUSICEXT:
  M_Print(16, 32 + 8 * OPT_MUSICEXT, "        External Music");
  M_DrawCheckbox(220, 32 + 8 * OPT_MUSICEXT, bgm_extmusic.value);

  // OPT_ALWAYRUN:
  M_Print(16, 32 + 8 * OPT_ALWAYRUN, "            Always Run");
  if (cl_alwaysrun.value)
    M_Print(220, 32 + 8 * OPT_ALWAYRUN, "quakespasm");
  else if (cl_forwardspeed.value > 200.0)
    M_Print(220, 32 + 8 * OPT_ALWAYRUN, "vanilla");
  else
    M_Print(220, 32 + 8 * OPT_ALWAYRUN, "off");

  // OPT_INVMOUSE:
  M_Print(16, 32 + 8 * OPT_INVMOUSE, "          Invert Mouse");
  M_DrawCheckbox(220, 32 + 8 * OPT_INVMOUSE, m_pitch.value < 0);

  // OPT_ALWAYSMLOOK:
  M_Print(16, 32 + 8 * OPT_ALWAYSMLOOK, "            Mouse Look");
  M_DrawCheckbox(220, 32 + 8 * OPT_ALWAYSMLOOK, in_mlook.state & 1);

  // OPT_LOOKSPRING:
  M_Print(16, 32 + 8 * OPT_LOOKSPRING, "            Lookspring");
  M_DrawCheckbox(220, 32 + 8 * OPT_LOOKSPRING, lookspring.value);

  // OPT_LOOKSTRAFE:
  M_Print(16, 32 + 8 * OPT_LOOKSTRAFE, "            Lookstrafe");
  M_DrawCheckbox(220, 32 + 8 * OPT_LOOKSTRAFE, lookstrafe.value);

  // OPT_VIDEO:
  if (vid_menudrawfn)
    M_Print(16, 32 + 8 * OPT_VIDEO, "         Video Options");

  // OPT_VR:
  if (vid_menudrawfn)
    M_Print(16, 32 + 8 * OPT_VR, "         VR Options");

  // OPT_MODELS:
  M_Print(16, 32 + 8 * OPT_MODELS, "        Models Options");

  // cursor
  M_DrawCharacter(200, 32 + options_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Options_Key(int k) {
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    m_entersound = true;
    switch (options_cursor) {
    case OPT_CUSTOMIZE:
      M_Menu_Keys_f();
      break;
    case OPT_WEAPONS:
      M_Menu_Weapons_f();
      break;
    case OPT_CONSOLE:
      m_state = m_none;
      Con_ToggleConsole_f();
      break;
    case OPT_DEFAULTS:
      if (SCR_ModalMessage("This will reset all controls\n"
                           "and stored cvars. Continue? (y/n)\n",
                           15.0f)) {
        Cbuf_AddText("resetcfg\n");
        Cbuf_AddText("exec default.cfg\n");
      }
      break;
    case OPT_VIDEO:
      M_Menu_Video_f();
      break;
    case OPT_VR:
      M_Menu_VR_f();
      break;
    case OPT_MODELS:
      M_Menu_Models_f();
      break;
    default:
      M_AdjustSliders(1);
      break;
    }
    return;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    options_cursor--;
    if (options_cursor < 0)
      options_cursor = OPTIONS_ITEMS - 1;
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    options_cursor++;
    if (options_cursor >= OPTIONS_ITEMS)
      options_cursor = 0;
    break;

  case K_LEFTARROW:
    M_AdjustSliders(-1);
    break;

  case K_RIGHTARROW:
    M_AdjustSliders(1);
    break;
  }

  if (!vid_menudrawfn && (k == K_UPARROW || k == K_DOWNARROW)) {
    int dir = k == K_UPARROW ? -1 : 1;

    while (options_cursor == OPT_VIDEO || options_cursor == OPT_VR) {
      options_cursor += dir;
      if (options_cursor < 0)
        options_cursor = OPTIONS_ITEMS - 1;
      else if (options_cursor >= OPTIONS_ITEMS)
        options_cursor = 0;
    }
  }
}

//=============================================================================
/* KEYS MENU */

const char *bindnames[][2] = {{"+attack", "attack"},
                              {"+button3", "alt fire"},
                              {"impulse 10", "next weapon"},
                              {"impulse 12", "prev weapon"},
                              {"+jump", "jump / swim up"},
                              {"+forward", "walk forward"},
                              {"+back", "backpedal"},
                              {"+left", "turn left"},
                              {"+right", "turn right"},
                              {"+speed", "run"},
                              {"+moveleft", "step left"},
                              {"+moveright", "step right"},
                              {"+strafe", "sidestep"},
                              {"+lookup", "look up"},
                              {"+lookdown", "look down"},
                              {"centerview", "center view"},
                              {"+mlook", "mouse look"},
                              {"+klook", "keyboard look"},
                              {"+moveup", "swim up"},
                              {"+movedown", "swim down"},
                              {"+vr_weaponmenu", "VR weapon wheel"},
                              {"vr_turn180", "180 snap turn"}};

#define NUMCOMMANDS Q_COUNTOF(bindnames)

static int keys_cursor;
static qboolean bind_grab;

void M_Menu_Keys_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_keys;
  m_entersound = true;
}

void M_FindKeysForCommand(const char *command, int *threekeys) {
  int count;
  int j;
  char *b;

  threekeys[0] = threekeys[1] = threekeys[2] = -1;
  count = 0;

  for (j = 0; j < MAX_KEYS; j++) {
    b = keybindings[j];
    if (!b)
      continue;
    if (!strcmp(b, command)) {
      threekeys[count] = j;
      count++;
      if (count == 3)
        break;
    }
  }
}

void M_UnbindCommand(const char *command) {
  int j;
  char *b;

  for (j = 0; j < MAX_KEYS; j++) {
    b = keybindings[j];
    if (!b)
      continue;
    if (!strcmp(b, command))
      Key_SetBinding(j, NULL);
  }
}

extern qpic_t *pic_up, *pic_down;

void M_Keys_Draw(void) {
  int i, x, y;
  int keys[3];
  const char *name;
  qpic_t *p;

  p = Draw_CachePic("gfx/ttl_cstm.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);

  if (bind_grab)
    M_Print(12, 32, "Press a key or button for this action");
  else
    M_Print(18, 32, "Enter to change, backspace to clear");

  // search for known bindings
  for (i = 0; i < (int)NUMCOMMANDS; i++) {
    y = 48 + 8 * i;

    M_Print(16, y, bindnames[i][1]);

    M_FindKeysForCommand(bindnames[i][0], keys);

    if (keys[0] == -1) {
      M_Print(140, y, "???");
    } else {
      name = Key_KeynumToString(keys[0]);
      M_Print(140, y, name);
      x = strlen(name) * 8;
      if (keys[1] != -1) {
        name = Key_KeynumToString(keys[1]);
        M_Print(140 + x + 8, y, "or");
        M_Print(140 + x + 32, y, name);
        x = x + 32 + strlen(name) * 8;
        if (keys[2] != -1) {
          M_Print(140 + x + 8, y, "or");
          M_Print(140 + x + 32, y, Key_KeynumToString(keys[2]));
        }
      }
    }
  }

  if (bind_grab)
    M_DrawCharacter(130, 48 + keys_cursor * 8, '=');
  else
    M_DrawCharacter(130, 48 + keys_cursor * 8, 12 + ((int)(realtime * 4) & 1));
}

void M_Keys_Key(int k) {
  char cmd[80];
  int keys[3];

  if (bind_grab) { // defining a key
    S_LocalSound("misc/menu1.wav");
    if ((k != K_ESCAPE) && (k != '`')) {
      sprintf(cmd, "bind \"%s\" \"%s\"\n", Key_KeynumToString(k),
              bindnames[keys_cursor][0]);
      Cbuf_InsertText(cmd);
    }

    bind_grab = false;
    IN_Deactivate(
        modestate ==
        MS_WINDOWED); // deactivate because we're returning to the menu
    return;
  }

  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Options_f();
    break;

  case K_LEFTARROW:
  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    keys_cursor--;
    if (keys_cursor < 0)
      keys_cursor = NUMCOMMANDS - 1;
    break;

  case K_DOWNARROW:
  case K_RIGHTARROW:
    S_LocalSound("misc/menu1.wav");
    keys_cursor++;
    if (keys_cursor >= (int)NUMCOMMANDS)
      keys_cursor = 0;
    break;

  case K_ENTER: // go into bind mode
  case K_KP_ENTER:
  case K_ABUTTON:
    M_FindKeysForCommand(bindnames[keys_cursor][0], keys);
    S_LocalSound("misc/menu2.wav");
    if (keys[2] != -1)
      M_UnbindCommand(bindnames[keys_cursor][0]);
    bind_grab = true;
    IN_Activate(); // activate to allow mouse key binding
    break;

  case K_BACKSPACE: // delete bindings
  case K_DEL:
    S_LocalSound("misc/menu2.wav");
    M_UnbindCommand(bindnames[keys_cursor][0]);
    break;
  }
}

//=============================================================================
/* WEAPON BINDINGS MENU -- compact Ironwail-style direct weapon access. */

static const char *weapon_bindnames[][2] = {
    {"+attack", "attack"},
    {"impulse 10", "next weapon"},
    {"impulse 12", "previous weapon"},
    {"impulse 1", "axe"},
    {"impulse 2", "shotgun"},
    {"impulse 3", "super shotgun"},
    {"impulse 4", "nailgun"},
    {"impulse 5", "super nailgun"},
    {"impulse 6", "grenade launcher"},
    {"impulse 7", "rocket launcher"},
    {"impulse 8", "thunderbolt"},
    {"impulse 225", "hipnotic laser"},
    {"impulse 226", "hipnotic mjolnir"},
};

#define NUM_WEAPON_COMMANDS Q_COUNTOF(weapon_bindnames)

static int weapons_cursor;

static void M_Menu_Weapons_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_weapons;
  m_entersound = true;
}

static void M_Weapons_Draw(void) {
  int i, x, y, keys[3];
  const char *name;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  M_PrintWhite(112, 8, "WEAPON BINDS");
  if (bind_grab)
    M_Print(12, 32, "Press a key or button for this action");
  else
    M_Print(18, 32, "Enter to change, backspace to clear");

  for (i = 0; i < (int)NUM_WEAPON_COMMANDS; i++) {
    y = 48 + 8 * i;
    M_Print(16, y, weapon_bindnames[i][1]);
    M_FindKeysForCommand(weapon_bindnames[i][0], keys);
    if (keys[0] == -1) {
      M_Print(184, y, "???");
      continue;
    }

    name = Key_KeynumToString(keys[0]);
    M_Print(184, y, name);
    x = strlen(name) * 8;
    if (keys[1] != -1) {
      name = Key_KeynumToString(keys[1]);
      M_Print(184 + x + 8, y, "or");
      M_Print(184 + x + 32, y, name);
    }
  }

  M_DrawCharacter(176, 48 + weapons_cursor * 8,
                  bind_grab ? '=' : 12 + ((int)(realtime * 4) & 1));
}

static void M_Weapons_Key(int k) {
  char cmd[80];
  int keys[3];

  if (bind_grab) {
    S_LocalSound("misc/menu1.wav");
    if (k != K_ESCAPE && k != '`') {
      q_snprintf(cmd, sizeof(cmd), "bind \"%s\" \"%s\"\n",
                 Key_KeynumToString(k), weapon_bindnames[weapons_cursor][0]);
      Cbuf_InsertText(cmd);
    }
    bind_grab = false;
    IN_Deactivate(modestate == MS_WINDOWED);
    return;
  }

  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Options_f();
    break;
  case K_LEFTARROW:
  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    if (--weapons_cursor < 0)
      weapons_cursor = NUM_WEAPON_COMMANDS - 1;
    break;
  case K_DOWNARROW:
  case K_RIGHTARROW:
    S_LocalSound("misc/menu1.wav");
    if (++weapons_cursor >= (int)NUM_WEAPON_COMMANDS)
      weapons_cursor = 0;
    break;
  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    M_FindKeysForCommand(weapon_bindnames[weapons_cursor][0], keys);
    S_LocalSound("misc/menu2.wav");
    if (keys[2] != -1)
      M_UnbindCommand(weapon_bindnames[weapons_cursor][0]);
    bind_grab = true;
    IN_Activate();
    break;
  case K_BACKSPACE:
  case K_DEL:
    S_LocalSound("misc/menu2.wav");
    M_UnbindCommand(weapon_bindnames[weapons_cursor][0]);
    break;
  }
}

//=============================================================================
/* VIDEO MENU */

void M_Menu_Video_f(void) {
  (*vid_menucmdfn)(); // johnfitz
}

void M_Video_Draw(void) { (*vid_menudrawfn)(); }

void M_Video_Key(int key) { (*vid_menukeyfn)(key); }

//=============================================================================
/* VR MENU */

void M_Menu_VR_f(void) {
  if (vr_menucmdfn) {
    (*vr_menucmdfn)();
  }
}

void M_VR_Draw(void) {
  if (vr_menudrawfn) {
    (*vr_menudrawfn)();
  }
}

void M_VR_Key(int key) {
  if (vr_menukeyfn) {
    (*vr_menukeyfn)(key);
  }
}

//=============================================================================
/* HELP MENU */

int help_page;
#define NUM_HELP_PAGES 6

void M_Menu_Help_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_help;
  m_entersound = true;
  help_page = 0;
}

void M_Help_Draw(void) {
  M_DrawPic(0, 0, Draw_CachePic(va("gfx/help%i.lmp", help_page)));
}

void M_Help_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_UPARROW:
  case K_RIGHTARROW:
    m_entersound = true;
    if (++help_page >= NUM_HELP_PAGES)
      help_page = 0;
    break;

  case K_DOWNARROW:
  case K_LEFTARROW:
    m_entersound = true;
    if (--help_page < 0)
      help_page = NUM_HELP_PAGES - 1;
    break;
  }
}

//=============================================================================
/* QUIT MENU */

int msgNumber;
enum m_state_e m_quit_prevstate;
qboolean wasInMenus;

void M_Menu_Quit_f(void) {
  if (m_state == m_quit)
    return;
  wasInMenus = (key_dest == key_menu);
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_quit_prevstate = m_state;
  m_state = m_quit;
  m_entersound = true;
  msgNumber = rand() & 7;
}

void M_Quit_Key(int key) {
  if (key == K_ESCAPE) {
    if (wasInMenus) {
      m_state = m_quit_prevstate;
      m_entersound = true;
    } else {
      IN_Activate();
      key_dest = key_game;
      m_state = m_none;
    }
  }
}

void M_Quit_Char(int key) {
  switch (key) {
  case 'n':
  case 'N':
    if (wasInMenus) {
      m_state = m_quit_prevstate;
      m_entersound = true;
    } else {
      IN_Activate();
      key_dest = key_game;
      m_state = m_none;
    }
    break;

  case 'y':
  case 'Y':
    IN_Deactivate(modestate == MS_WINDOWED);
    key_dest = key_console;
    Host_Quit_f();
    break;

  default:
    break;
  }
}

qboolean M_Quit_TextEntry(void) { return true; }

void M_Quit_Draw(void) // johnfitz -- modified for new quit message
{
  char msg1[] = "QuakeSpasm " QUAKESPASM_VER_STRING;
  char msg2[] =
      "by Ozkan Sezer,Eric Wasylishen,others"; /* msg2/msg3 are [38] at most */
  char msg3[] = "Press y to quit";
  int boxlen;

  if (wasInMenus) {
    m_state = m_quit_prevstate;
    m_recursiveDraw = true;
    M_Draw();
    m_state = m_quit;
  }

  // okay, this is kind of fucked up.  M_DrawTextBox will always act as if
  // width is even. Also, the width and lines values are for the interior of the
  // box, but the x and y values include the border.
  boxlen = (q_max(sizeof(msg1), q_max(sizeof(msg2), sizeof(msg3))) + 1) & ~1;
  M_DrawTextBox(160 - 4 * (boxlen + 2), 76, boxlen, 4);

  // now do the text
  M_Print(160 - 4 * (sizeof(msg1) - 1), 88, msg1);
  M_Print(160 - 4 * (sizeof(msg2) - 1), 96, msg2);
  M_PrintWhite(160 - 4 * (sizeof(msg3) - 1), 104, msg3);
}

//=============================================================================
/* LAN CONFIG MENU */

int lanConfig_cursor = -1;
int lanConfig_cursor_table[] = {72, 92, 124};
#define NUM_LANCONFIG_CMDS 3

int lanConfig_port;
char lanConfig_portname[6];
char lanConfig_joinname[22];

void M_Menu_LanConfig_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_lanconfig;
  m_entersound = true;
  if (lanConfig_cursor == -1) {
    if (JoiningGame && TCPIPConfig)
      lanConfig_cursor = 2;
    else
      lanConfig_cursor = 1;
  }
  if (StartingGame && lanConfig_cursor == 2)
    lanConfig_cursor = 1;
  lanConfig_port = DEFAULTnet_hostport;
  sprintf(lanConfig_portname, "%u", lanConfig_port);

  m_return_onerror = false;
  m_return_reason[0] = 0;
}

void M_LanConfig_Draw(void) {
  qpic_t *p;
  int basex;
  const char *startJoin;
  const char *protocol;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  p = Draw_CachePic("gfx/p_multi.lmp");
  basex = (320 - p->width) / 2;
  M_DrawPic(basex, 4, p);

  basex = 72; /* Arcane Dimensions has an oversized gfx/p_multi.lmp */

  if (StartingGame)
    startJoin = "New Game";
  else
    startJoin = "Join Game";
  if (IPXConfig)
    protocol = "IPX";
  else
    protocol = "TCP/IP";
  M_Print(basex, 32, va("%s - %s", startJoin, protocol));
  basex += 8;

  M_Print(basex, 52, "Address:");
  if (IPXConfig)
    M_Print(basex + 9 * 8, 52, my_ipx_address);
  else
    M_Print(basex + 9 * 8, 52, my_tcpip_address);

  M_Print(basex, lanConfig_cursor_table[0], "Port");
  M_DrawTextBox(basex + 8 * 8, lanConfig_cursor_table[0] - 8, 6, 1);
  M_Print(basex + 9 * 8, lanConfig_cursor_table[0], lanConfig_portname);

  if (JoiningGame) {
    M_Print(basex, lanConfig_cursor_table[1], "Search for local games...");
    M_Print(basex, 108, "Join game at:");
    M_DrawTextBox(basex + 8, lanConfig_cursor_table[2] - 8, 22, 1);
    M_Print(basex + 16, lanConfig_cursor_table[2], lanConfig_joinname);
  } else {
    M_DrawTextBox(basex, lanConfig_cursor_table[1] - 8, 2, 1);
    M_Print(basex + 8, lanConfig_cursor_table[1], "OK");
  }

  M_DrawCharacter(basex - 8, lanConfig_cursor_table[lanConfig_cursor],
                  12 + ((int)(realtime * 4) & 1));

  if (lanConfig_cursor == 0)
    M_DrawCharacter(basex + 9 * 8 + 8 * strlen(lanConfig_portname),
                    lanConfig_cursor_table[0], 10 + ((int)(realtime * 4) & 1));

  if (lanConfig_cursor == 2)
    M_DrawCharacter(basex + 16 + 8 * strlen(lanConfig_joinname),
                    lanConfig_cursor_table[2], 10 + ((int)(realtime * 4) & 1));

  if (*m_return_reason)
    M_PrintWhite(basex, 148, m_return_reason);
}

void M_LanConfig_Key(int key) {
  int l;

  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Net_f();
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    lanConfig_cursor--;
    if (lanConfig_cursor < 0)
      lanConfig_cursor = NUM_LANCONFIG_CMDS - 1;
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    lanConfig_cursor++;
    if (lanConfig_cursor >= NUM_LANCONFIG_CMDS)
      lanConfig_cursor = 0;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    if (lanConfig_cursor == 0)
      break;

    m_entersound = true;

    M_ConfigureNetSubsystem();

    if (lanConfig_cursor == 1) {
      if (StartingGame) {
        M_Menu_GameOptions_f();
        break;
      }
      M_Menu_Search_f();
      break;
    }

    if (lanConfig_cursor == 2) {
      m_return_state = m_state;
      m_return_onerror = true;
      IN_Activate();
      key_dest = key_game;
      m_state = m_none;
      Cbuf_AddText(va("connect \"%s\"\n", lanConfig_joinname));
      break;
    }

    break;

  case K_BACKSPACE:
    if (lanConfig_cursor == 0) {
      if (strlen(lanConfig_portname))
        lanConfig_portname[strlen(lanConfig_portname) - 1] = 0;
    }

    if (lanConfig_cursor == 2) {
      if (strlen(lanConfig_joinname))
        lanConfig_joinname[strlen(lanConfig_joinname) - 1] = 0;
    }
    break;
  }

  if (StartingGame && lanConfig_cursor == 2) {
    if (key == K_UPARROW)
      lanConfig_cursor = 1;
    else
      lanConfig_cursor = 0;
  }

  l = Q_atoi(lanConfig_portname);
  if (l > 65535)
    l = lanConfig_port;
  else
    lanConfig_port = l;
  sprintf(lanConfig_portname, "%u", lanConfig_port);
}

void M_LanConfig_Char(int key) {
  int l;

  switch (lanConfig_cursor) {
  case 0:
    if (key < '0' || key > '9')
      return;
    l = strlen(lanConfig_portname);
    if (l < 5) {
      lanConfig_portname[l + 1] = 0;
      lanConfig_portname[l] = key;
    }
    break;
  case 2:
    l = strlen(lanConfig_joinname);
    if (l < 21) {
      lanConfig_joinname[l + 1] = 0;
      lanConfig_joinname[l] = key;
    }
    break;
  }
}

qboolean M_LanConfig_TextEntry(void) {
  return (lanConfig_cursor == 0 || lanConfig_cursor == 2);
}

//=============================================================================
/* GAME OPTIONS MENU */

typedef struct {
  const char *name;
  const char *description;
} level_t;

level_t levels[] = {{"start", "Entrance"}, // 0

                    {"e1m1", "Slipgate Complex"}, // 1
                    {"e1m2", "Castle of the Damned"},
                    {"e1m3", "The Necropolis"},
                    {"e1m4", "The Grisly Grotto"},
                    {"e1m5", "Gloom Keep"},
                    {"e1m6", "The Door To Chthon"},
                    {"e1m7", "The House of Chthon"},
                    {"e1m8", "Ziggurat Vertigo"},

                    {"e2m1", "The Installation"}, // 9
                    {"e2m2", "Ogre Citadel"},
                    {"e2m3", "Crypt of Decay"},
                    {"e2m4", "The Ebon Fortress"},
                    {"e2m5", "The Wizard's Manse"},
                    {"e2m6", "The Dismal Oubliette"},
                    {"e2m7", "Underearth"},

                    {"e3m1", "Termination Central"}, // 16
                    {"e3m2", "The Vaults of Zin"},
                    {"e3m3", "The Tomb of Terror"},
                    {"e3m4", "Satan's Dark Delight"},
                    {"e3m5", "Wind Tunnels"},
                    {"e3m6", "Chambers of Torment"},
                    {"e3m7", "The Haunted Halls"},

                    {"e4m1", "The Sewage System"}, // 23
                    {"e4m2", "The Tower of Despair"},
                    {"e4m3", "The Elder God Shrine"},
                    {"e4m4", "The Palace of Hate"},
                    {"e4m5", "Hell's Atrium"},
                    {"e4m6", "The Pain Maze"},
                    {"e4m7", "Azure Agony"},
                    {"e4m8", "The Nameless City"},

                    {"end", "Shub-Niggurath's Pit"}, // 31

                    {"dm1", "Place of Two Deaths"}, // 32
                    {"dm2", "Claustrophobopolis"},
                    {"dm3", "The Abandoned Base"},
                    {"dm4", "The Bad Place"},
                    {"dm5", "The Cistern"},
                    {"dm6", "The Dark Zone"}};

// MED 01/06/97 added hipnotic levels
level_t hipnoticlevels[] = {
    {"start", "Command HQ"}, // 0

    {"hip1m1", "The Pumping Station"}, // 1
    {"hip1m2", "Storage Facility"},
    {"hip1m3", "The Lost Mine"},
    {"hip1m4", "Research Facility"},
    {"hip1m5", "Military Complex"},

    {"hip2m1", "Ancient Realms"}, // 6
    {"hip2m2", "The Black Cathedral"},
    {"hip2m3", "The Catacombs"},
    {"hip2m4", "The Crypt"},
    {"hip2m5", "Mortum's Keep"},
    {"hip2m6", "The Gremlin's Domain"},

    {"hip3m1", "Tur Torment"}, // 12
    {"hip3m2", "Pandemonium"},
    {"hip3m3", "Limbo"},
    {"hip3m4", "The Gauntlet"},

    {"hipend", "Armagon's Lair"}, // 16

    {"hipdm1", "The Edge of Oblivion"} // 17
};

// PGM 01/07/97 added rogue levels
// PGM 03/02/97 added dmatch level
level_t roguelevels[] = {
    {"start", "Split Decision"},   {"r1m1", "Deviant's Domain"},
    {"r1m2", "Dread Portal"},      {"r1m3", "Judgement Call"},
    {"r1m4", "Cave of Death"},     {"r1m5", "Towers of Wrath"},
    {"r1m6", "Temple of Pain"},    {"r1m7", "Tomb of the Overlord"},
    {"r2m1", "Tempus Fugit"},      {"r2m2", "Elemental Fury I"},
    {"r2m3", "Elemental Fury II"}, {"r2m4", "Curse of Osiris"},
    {"r2m5", "Wizard's Keep"},     {"r2m6", "Blood Sacrifice"},
    {"r2m7", "Last Bastion"},      {"r2m8", "Source of Evil"},
    {"ctf1", "Division of Change"}};

typedef struct {
  const char *description;
  int firstLevel;
  int levels;
} episode_t;

episode_t episodes[] = {
    {"Welcome to Quake", 0, 1},     {"Doomed Dimension", 1, 8},
    {"Realm of Black Magic", 9, 7}, {"Netherworld", 16, 7},
    {"The Elder World", 23, 8},     {"Final Level", 31, 1},
    {"Deathmatch Arena", 32, 6}};

// MED 01/06/97  added hipnotic episodes
episode_t hipnoticepisodes[] = {
    {"Scourge of Armagon", 0, 1},   {"Fortress of the Dead", 1, 5},
    {"Dominion of Darkness", 6, 6}, {"The Rift", 12, 4},
    {"Final Level", 16, 1},         {"Deathmatch Arena", 17, 1}};

// PGM 01/07/97 added rogue episodes
// PGM 03/02/97 added dmatch episode
episode_t rogueepisodes[] = {{"Introduction", 0, 1},
                             {"Hell's Fortress", 1, 7},
                             {"Corridors of Time", 8, 8},
                             {"Deathmatch Arena", 16, 1}};

int startepisode;
int startlevel;
int maxplayers;
qboolean m_serverInfoMessage = false;
double m_serverInfoMessageTime;

void M_Menu_GameOptions_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_gameoptions;
  m_entersound = true;
  if (maxplayers == 0)
    maxplayers = svs.maxclients;
  if (maxplayers < 2)
    maxplayers = svs.maxclientslimit;
}

int gameoptions_cursor_table[] = {40, 56, 64, 72, 80, 88, 96, 112, 120};
#define NUM_GAMEOPTIONS 9
int gameoptions_cursor;

void M_GameOptions_Draw(void) {
  qpic_t *p;
  int x;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  p = Draw_CachePic("gfx/p_multi.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);

  M_DrawTextBox(152, 32, 10, 1);
  M_Print(160, 40, "begin game");

  M_Print(0, 56, "      Max players");
  M_Print(160, 56, va("%i", maxplayers));

  M_Print(0, 64, "        Game Type");
  if (coop.value)
    M_Print(160, 64, "Cooperative");
  else
    M_Print(160, 64, "Deathmatch");

  M_Print(0, 72, "        Teamplay");
  if (rogue) {
    const char *msg;

    switch ((int)teamplay.value) {
    case 1:
      msg = "No Friendly Fire";
      break;
    case 2:
      msg = "Friendly Fire";
      break;
    case 3:
      msg = "Tag";
      break;
    case 4:
      msg = "Capture the Flag";
      break;
    case 5:
      msg = "One Flag CTF";
      break;
    case 6:
      msg = "Three Team CTF";
      break;
    default:
      msg = "Off";
      break;
    }
    M_Print(160, 72, msg);
  } else {
    const char *msg;

    switch ((int)teamplay.value) {
    case 1:
      msg = "No Friendly Fire";
      break;
    case 2:
      msg = "Friendly Fire";
      break;
    default:
      msg = "Off";
      break;
    }
    M_Print(160, 72, msg);
  }

  M_Print(0, 80, "            Skill");
  if (skill.value == 0)
    M_Print(160, 80, "Easy difficulty");
  else if (skill.value == 1)
    M_Print(160, 80, "Normal difficulty");
  else if (skill.value == 2)
    M_Print(160, 80, "Hard difficulty");
  else
    M_Print(160, 80, "Nightmare difficulty");

  M_Print(0, 88, "       Frag Limit");
  if (fraglimit.value == 0)
    M_Print(160, 88, "none");
  else
    M_Print(160, 88, va("%i frags", (int)fraglimit.value));

  M_Print(0, 96, "       Time Limit");
  if (timelimit.value == 0)
    M_Print(160, 96, "none");
  else
    M_Print(160, 96, va("%i minutes", (int)timelimit.value));

  M_Print(0, 112, "         Episode");
  // MED 01/06/97 added hipnotic episodes
  if (hipnotic)
    M_Print(160, 112, hipnoticepisodes[startepisode].description);
  // PGM 01/07/97 added rogue episodes
  else if (rogue)
    M_Print(160, 112, rogueepisodes[startepisode].description);
  else
    M_Print(160, 112, episodes[startepisode].description);

  M_Print(0, 120, "           Level");
  // MED 01/06/97 added hipnotic episodes
  if (hipnotic) {
    M_Print(
        160, 120,
        hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel]
            .description);
    M_Print(
        160, 128,
        hipnoticlevels[hipnoticepisodes[startepisode].firstLevel + startlevel]
            .name);
  }
  // PGM 01/07/97 added rogue episodes
  else if (rogue) {
    M_Print(160, 120,
            roguelevels[rogueepisodes[startepisode].firstLevel + startlevel]
                .description);
    M_Print(
        160, 128,
        roguelevels[rogueepisodes[startepisode].firstLevel + startlevel].name);
  } else {
    M_Print(160, 120,
            levels[episodes[startepisode].firstLevel + startlevel].description);
    M_Print(160, 128,
            levels[episodes[startepisode].firstLevel + startlevel].name);
  }

  // line cursor
  M_DrawCharacter(144, gameoptions_cursor_table[gameoptions_cursor],
                  12 + ((int)(realtime * 4) & 1));

  if (m_serverInfoMessage) {
    if ((realtime - m_serverInfoMessageTime) < 5.0) {
      x = (320 - 26 * 8) / 2;
      M_DrawTextBox(x, 138, 24, 4);
      x += 8;
      M_Print(x, 146, "  More than 4 players   ");
      M_Print(x, 154, " requires using command ");
      M_Print(x, 162, "line parameters; please ");
      M_Print(x, 170, "   see techinfo.txt.    ");
    } else {
      m_serverInfoMessage = false;
    }
  }
}

void M_NetStart_Change(int dir) {
  int count;
  float f;

  switch (gameoptions_cursor) {
  case 1:
    maxplayers += dir;
    if (maxplayers > svs.maxclientslimit) {
      maxplayers = svs.maxclientslimit;
      m_serverInfoMessage = true;
      m_serverInfoMessageTime = realtime;
    }
    if (maxplayers < 2)
      maxplayers = 2;
    break;

  case 2:
    Cvar_Set("coop", coop.value ? "0" : "1");
    break;

  case 3:
    count = (rogue) ? 6 : 2;
    f = teamplay.value + dir;
    if (f > count)
      f = 0;
    else if (f < 0)
      f = count;
    Cvar_SetValue("teamplay", f);
    break;

  case 4:
    f = skill.value + dir;
    if (f > 3)
      f = 0;
    else if (f < 0)
      f = 3;
    Cvar_SetValue("skill", f);
    break;

  case 5:
    f = fraglimit.value + dir * 10;
    if (f > 100)
      f = 0;
    else if (f < 0)
      f = 100;
    Cvar_SetValue("fraglimit", f);
    break;

  case 6:
    f = timelimit.value + dir * 5;
    if (f > 60)
      f = 0;
    else if (f < 0)
      f = 60;
    Cvar_SetValue("timelimit", f);
    break;

  case 7:
    startepisode += dir;
    // MED 01/06/97 added hipnotic count
    if (hipnotic)
      count = 6;
    // PGM 01/07/97 added rogue count
    // PGM 03/02/97 added 1 for dmatch episode
    else if (rogue)
      count = 4;
    else if (registered.value)
      count = 7;
    else
      count = 2;

    if (startepisode < 0)
      startepisode = count - 1;

    if (startepisode >= count)
      startepisode = 0;

    startlevel = 0;
    break;

  case 8:
    startlevel += dir;
    // MED 01/06/97 added hipnotic episodes
    if (hipnotic)
      count = hipnoticepisodes[startepisode].levels;
    // PGM 01/06/97 added hipnotic episodes
    else if (rogue)
      count = rogueepisodes[startepisode].levels;
    else
      count = episodes[startepisode].levels;

    if (startlevel < 0)
      startlevel = count - 1;

    if (startlevel >= count)
      startlevel = 0;
    break;
  }
}

void M_GameOptions_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Net_f();
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    gameoptions_cursor--;
    if (gameoptions_cursor < 0)
      gameoptions_cursor = NUM_GAMEOPTIONS - 1;
    break;

  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    gameoptions_cursor++;
    if (gameoptions_cursor >= NUM_GAMEOPTIONS)
      gameoptions_cursor = 0;
    break;

  case K_LEFTARROW:
    if (gameoptions_cursor == 0)
      break;
    S_LocalSound("misc/menu3.wav");
    M_NetStart_Change(-1);
    break;

  case K_RIGHTARROW:
    if (gameoptions_cursor == 0)
      break;
    S_LocalSound("misc/menu3.wav");
    M_NetStart_Change(1);
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    S_LocalSound("misc/menu2.wav");
    if (gameoptions_cursor == 0) {
      if (sv.active)
        Cbuf_AddText("disconnect\n");
      Cbuf_AddText("listen 0\n"); // so host_netport will be re-examined
      Cbuf_AddText(va("maxplayers %u\n", maxplayers));
      SCR_BeginLoadingPlaque();

      if (hipnotic)
        Cbuf_AddText(
            va("map %s\n",
               hipnoticlevels[hipnoticepisodes[startepisode].firstLevel +
                              startlevel]
                   .name));
      else if (rogue)
        Cbuf_AddText(
            va("map %s\n",
               roguelevels[rogueepisodes[startepisode].firstLevel + startlevel]
                   .name));
      else
        Cbuf_AddText(
            va("map %s\n",
               levels[episodes[startepisode].firstLevel + startlevel].name));

      return;
    }

    M_NetStart_Change(1);
    break;
  }
}

//=============================================================================
/* SEARCH MENU */

qboolean searchComplete = false;
double searchCompleteTime;

void M_Menu_Search_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_search;
  m_entersound = false;
  slistSilent = true;
  slistLocal = false;
  searchComplete = false;
  NET_Slist_f();
}

void M_Search_Draw(void) {
  qpic_t *p;
  int x;

  p = Draw_CachePic("gfx/p_multi.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);
  x = (320 / 2) - ((12 * 8) / 2) + 4;
  M_DrawTextBox(x - 8, 32, 12, 1);
  M_Print(x, 40, "Searching...");

  if (slistInProgress) {
    NET_Poll();
    return;
  }

  if (!searchComplete) {
    searchComplete = true;
    searchCompleteTime = realtime;
  }

  if (hostCacheCount) {
    M_Menu_ServerList_f();
    return;
  }

  M_PrintWhite((320 / 2) - ((22 * 8) / 2), 64, "No Quake servers found");
  if ((realtime - searchCompleteTime) < 3.0)
    return;

  M_Menu_LanConfig_f();
}

void M_Search_Key(int key) {}

//=============================================================================
/* SLIST MENU */

int slist_cursor;
qboolean slist_sorted;

void M_Menu_ServerList_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_slist;
  m_entersound = true;
  slist_cursor = 0;
  m_return_onerror = false;
  m_return_reason[0] = 0;
  slist_sorted = false;
}

void M_ServerList_Draw(void) {
  int n;
  qpic_t *p;

  if (!slist_sorted) {
    slist_sorted = true;
    NET_SlistSort();
  }

  p = Draw_CachePic("gfx/p_multi.lmp");
  M_DrawPic((320 - p->width) / 2, 4, p);
  for (n = 0; n < hostCacheCount; n++)
    M_Print(16, 32 + 8 * n, NET_SlistPrintServer(n));
  M_DrawCharacter(0, 32 + slist_cursor * 8, 12 + ((int)(realtime * 4) & 1));

  if (*m_return_reason)
    M_PrintWhite(16, 148, m_return_reason);
}

void M_ServerList_Key(int k) {
  switch (k) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_LanConfig_f();
    break;

  case K_SPACE:
    M_Menu_Search_f();
    break;

  case K_UPARROW:
  case K_LEFTARROW:
    S_LocalSound("misc/menu1.wav");
    slist_cursor--;
    if (slist_cursor < 0)
      slist_cursor = hostCacheCount - 1;
    break;

  case K_DOWNARROW:
  case K_RIGHTARROW:
    S_LocalSound("misc/menu1.wav");
    slist_cursor++;
    if (slist_cursor >= hostCacheCount)
      slist_cursor = 0;
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    S_LocalSound("misc/menu2.wav");
    m_return_state = m_state;
    m_return_onerror = true;
    slist_sorted = false;
    IN_Activate();
    key_dest = key_game;
    m_state = m_none;
    Cbuf_AddText(
        va("connect \"%s\"\n", NET_SlistPrintServerName(slist_cursor)));
    break;

  default:
    break;
  }
}

//=============================================================================
/* Credits menu -- used by the 2021 re-release */

void M_Menu_Credits_f(void) {}

//=============================================================================
/* MODS MENU */

#define MAX_MODS_ON_SCREEN 9
#define MODS_FILTER_MAX 22

/*
 * The installed browser and optional Ironwail-style catalogue share filtering,
 * paging, and pointer navigation. Catalogue network access remains explicit:
 * opening this menu never starts a download or changes the active game.
 */
static int m_mods_cursor; /* index among the currently matching add-ons */
static int m_mods_first;
static int m_mods_count;
static int m_mods_matches;
static char m_mods_filter[MODS_FILTER_MAX + 1];
static qboolean m_mods_catalogue;
static int m_mods_confirm = -1;

static qboolean M_Mods_IsActive(const filelist_item_t *item) {
  return !q_strcasecmp(item->name, COM_SkipPath(com_gamedir));
}

static qboolean M_Mods_Matches(const filelist_item_t *item) {
  return !m_mods_filter[0] || q_strcasestr(item->name, m_mods_filter) != NULL;
}

static qboolean M_Mods_CatalogueMatches(const addon_catalog_entry_t *item) {
  return !m_mods_filter[0] ||
         q_strcasestr(item->name, m_mods_filter) != NULL ||
         q_strcasestr(item->gamedir, m_mods_filter) != NULL ||
         q_strcasestr(item->author, m_mods_filter) != NULL;
}

static int M_Mods_CountMatches(void) {
  filelist_item_t *item;
  const addon_catalog_entry_t *catalogue;
  int count, index;

  if (m_mods_catalogue) {
    for (count = 0, index = 0; index < AddonCatalog_Count(); index++) {
      catalogue = AddonCatalog_Entry(index);
      if (catalogue && M_Mods_CatalogueMatches(catalogue))
        count++;
    }
    return count;
  }

  for (item = modlist, count = 0; item; item = item->next) {
    if (M_Mods_Matches(item))
      count++;
  }

  return count;
}

static int M_Mods_CatalogueIndex(int index) {
  int current;

  for (current = 0; current < AddonCatalog_Count(); current++) {
    const addon_catalog_entry_t *item = AddonCatalog_Entry(current);
    if (!item || !M_Mods_CatalogueMatches(item))
      continue;
    if (index-- == 0)
      return current;
  }
  return -1;
}

static const addon_catalog_entry_t *M_Mods_CatalogueItem(int index) {
  return AddonCatalog_Entry(M_Mods_CatalogueIndex(index));
}

static filelist_item_t *M_Mods_Item(int index) {
  filelist_item_t *item;

  for (item = modlist; item; item = item->next) {
    if (!M_Mods_Matches(item))
      continue;
    if (index-- == 0)
      return item;
  }

  return NULL;
}

static void M_Mods_KeepCursorVisible(void) {
  if (!m_mods_matches) {
    m_mods_cursor = 0;
    m_mods_first = 0;
    return;
  }

  m_mods_cursor = CLAMP(0, m_mods_cursor, m_mods_matches - 1);
  m_mods_first = CLAMP(0, m_mods_first,
                       q_max(0, m_mods_matches - MAX_MODS_ON_SCREEN));
  if (m_mods_cursor < m_mods_first)
    m_mods_first = m_mods_cursor;
  if (m_mods_cursor >= m_mods_first + MAX_MODS_ON_SCREEN)
    m_mods_first = m_mods_cursor - MAX_MODS_ON_SCREEN + 1;
}

static void M_Mods_Refresh(void) {
  filelist_item_t *item;

  AddonCatalog_Poll();
  if (m_mods_catalogue) {
    m_mods_count = AddonCatalog_Count();
    m_mods_matches = M_Mods_CountMatches();
    M_Mods_KeepCursorVisible();
    return;
  }

  for (item = modlist, m_mods_count = 0; item; item = item->next)
    m_mods_count++;

  m_mods_matches = M_Mods_CountMatches();
  M_Mods_KeepCursorVisible();
}

static void M_Mods_SelectActive(void) {
  filelist_item_t *item;
  int index;

  for (item = modlist, index = 0; item; item = item->next) {
    if (!M_Mods_Matches(item))
      continue;
    if (M_Mods_IsActive(item)) {
      m_mods_cursor = index;
      M_Mods_KeepCursorVisible();
      return;
    }
    index++;
  }
}

static void M_Menu_Mods_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_mods;
  m_entersound = true;

  /* Pick up add-ons copied into the game directory since engine startup. */
  Modlist_Rebuild();
  m_mods_filter[0] = 0;
  m_mods_catalogue = false;
  m_mods_confirm = -1;
  m_mods_cursor = 0;
  m_mods_first = 0;
  M_Mods_Refresh();
  M_Mods_SelectActive();
}

static void M_Mods_PrintName(int x, int y, const char *name, qboolean active) {
  char display[20];
  size_t len;

  q_strlcpy(display, name, sizeof(display));
  len = strlen(display);
  if (strlen(name) >= sizeof(display) && len >= 3) {
    display[len - 3] = '.';
    display[len - 2] = '.';
    display[len - 1] = '.';
  }

  if (active)
    M_PrintWhite(x, y, display);
  else
    M_Print(x, y, display);
}

static void M_Mods_DrawScrollbar(int x, int y) {
  int i, thumbfirst, thumblines;

  if (m_mods_matches <= MAX_MODS_ON_SCREEN)
    return;

  for (i = 0; i < MAX_MODS_ON_SCREEN; i++)
    M_Print(x, y + i * 8, ":");

  thumblines = q_max(1, MAX_MODS_ON_SCREEN * MAX_MODS_ON_SCREEN /
                            m_mods_matches);
  thumbfirst = m_mods_first * (MAX_MODS_ON_SCREEN - thumblines) /
               q_max(1, m_mods_matches - MAX_MODS_ON_SCREEN);
  for (i = 0; i < thumblines; i++)
    M_PrintWhite(x, y + (thumbfirst + i) * 8, "#");
}

static void M_Mods_Draw(void) {
  filelist_item_t *item;
  const addon_catalog_entry_t *catalogue;
  int i, last, visible;
  char count[32];
  const char *status;

  M_Mods_Refresh();

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  M_PrintWhite(144, 8, "MODS");
  M_DrawTextBox(16, 24, 34, 19);

  M_PrintWhite(32, 32, m_mods_catalogue ? "ADD-ON CATALOGUE" : "INSTALLED ADD-ONS");
  q_snprintf(count, sizeof(count), "%d %s", m_mods_count,
             m_mods_catalogue ? "available" : "installed");
  M_Print(288 - (int)strlen(count) * 8, 32, count);
  M_Print(32, 48, "NAME");
  M_Print(208, 48, "STATUS");
  M_Print(32, 56, "---------------------------------");

  if (!m_mods_count) {
    if (m_mods_catalogue) {
      if (AddonCatalog_State() == ADDON_CATALOG_REFRESHING)
        M_PrintWhite(80, 88, "Refreshing catalogue...");
      else if (vr_enabled.value)
        M_PrintWhite(48, 88, "Right A or point to refresh.");
      else
        M_PrintWhite(52, 88, "Press F1 to refresh catalogue.");
    } else {
      M_PrintWhite(48, 88, "No installed add-ons found.");
      M_Print(40, 104, "Add a Quake game directory beside id1.");
    }
  } else if (!m_mods_matches) {
    M_PrintWhite(72, 88, "No add-ons match this search.");
  } else {
    visible = q_min(MAX_MODS_ON_SCREEN, m_mods_matches - m_mods_first);
    for (i = 0; i < visible; i++) {
      int index = m_mods_first + i;
      if (m_mods_catalogue) {
        catalogue = M_Mods_CatalogueItem(index);
        if (!catalogue)
          continue;
        M_Mods_PrintName(48, 64 + i * 8, catalogue->name, false);
        if (catalogue->installed)
          status = "INSTALLED";
        else if (AddonCatalog_State() == ADDON_CATALOG_INSTALLING && index == m_mods_cursor)
          status = "DOWNLOADING";
        else if (m_mods_confirm == index)
          status = "CONFIRM";
        else
          status = "UNVERIFIED";
        M_Print(208, 64 + i * 8, status);
      } else {
        qboolean active;
        item = M_Mods_Item(index);
        if (!item)
          continue;
        active = M_Mods_IsActive(item);
        M_Mods_PrintName(48, 64 + i * 8, item->name, active);
        if (active)
          M_PrintWhite(208, 64 + i * 8, "ACTIVE");
        else
          M_Print(208, 64 + i * 8, "ready");
      }
    }

    M_DrawCharacter(40, 64 + (m_mods_cursor - m_mods_first) * 8,
                    12 + ((int)(realtime * 4) & 1));
    M_Mods_DrawScrollbar(288, 64);

    last = m_mods_first + visible;
    q_snprintf(count, sizeof(count), "%d-%d of %d", m_mods_first + 1, last,
               m_mods_matches);
    M_Print(288 - (int)strlen(count) * 8, 136, count);
  }

  M_Print(32, 152, "SEARCH");
  M_DrawTextBox(88, 144, 24, 1);
  M_Print(96, 152, m_mods_filter[0] ? m_mods_filter : "all installed add-ons");
  if ((int)(realtime * 4) & 1)
    M_DrawCharacter(96 + 8 * (int)strlen(m_mods_filter), 152, 10);

  if (m_mods_catalogue)
    M_Print(32, 168, AddonCatalog_Message());
  else
    M_Print(32, 168, "Enter: play     Type: search");
  if (vr_enabled.value) {
    if (m_mods_catalogue)
      M_Print(24, 176, "Trigger/L-A: select  R-A: refresh");
    else
      M_Print(32, 176, "Trigger/L-A: play  Stick: move");
    M_Print(32, 184, "[installed/catalogue]");
    if (m_mods_catalogue)
      M_Print(208, 184, "[refresh]");
  } else {
    M_Print(32, 176, "Tab: installed/catalogue");
    if (m_mods_catalogue)
      M_Print(32, 184, "F1: refresh  PgUp/PgDn: page");
    else
      M_Print(32, 184, "PgUp/PgDn: page Del: clear");
  }
}

static void M_Mods_MoveCursor(int amount) {
  if (!m_mods_matches)
    return;

  m_mods_cursor += amount;
  while (m_mods_cursor < 0)
    m_mods_cursor += m_mods_matches;
  while (m_mods_cursor >= m_mods_matches)
    m_mods_cursor -= m_mods_matches;
  m_mods_confirm = -1;
  M_Mods_KeepCursorVisible();
}

static void M_Mods_Key(int key) {
  filelist_item_t *item;

  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Main_f();
    break;

  case K_DOWNARROW:
  case K_RIGHTARROW:
  case K_MWHEELDOWN:
    if (m_mods_matches) {
      S_LocalSound("misc/menu1.wav");
      M_Mods_MoveCursor(1);
    }
    break;

  case K_UPARROW:
  case K_LEFTARROW:
  case K_MWHEELUP:
    if (m_mods_matches) {
      S_LocalSound("misc/menu1.wav");
      M_Mods_MoveCursor(-1);
    }
    break;

  case K_PGDN:
    if (m_mods_matches) {
      S_LocalSound("misc/menu1.wav");
      M_Mods_MoveCursor(MAX_MODS_ON_SCREEN);
    }
    break;

  case K_PGUP:
    if (m_mods_matches) {
      S_LocalSound("misc/menu1.wav");
      M_Mods_MoveCursor(-MAX_MODS_ON_SCREEN);
    }
    break;

  case K_HOME:
    if (m_mods_matches) {
      S_LocalSound("misc/menu1.wav");
      m_mods_cursor = 0;
      M_Mods_KeepCursorVisible();
    }
    break;

  case K_END:
    if (m_mods_matches) {
      S_LocalSound("misc/menu1.wav");
      m_mods_cursor = m_mods_matches - 1;
      M_Mods_KeepCursorVisible();
    }
    break;

  case K_BACKSPACE:
    if (m_mods_filter[0]) {
      m_mods_filter[strlen(m_mods_filter) - 1] = 0;
      m_mods_cursor = 0;
      m_mods_first = 0;
      m_mods_confirm = -1;
      M_Mods_Refresh();
      S_LocalSound("misc/menu1.wav");
    }
    break;

  case K_TAB:
    m_mods_catalogue = !m_mods_catalogue;
    if (!m_mods_catalogue)
      Modlist_Rebuild();
    m_mods_cursor = 0;
    m_mods_first = 0;
    m_mods_confirm = -1;
    M_Mods_Refresh();
    S_LocalSound("misc/menu1.wav");
    break;

  case K_DEL:
    if (m_mods_filter[0]) {
      m_mods_filter[0] = 0;
      m_mods_cursor = 0;
      m_mods_first = 0;
      m_mods_confirm = -1;
      M_Mods_Refresh();
      S_LocalSound("misc/menu1.wav");
    }
    break;

  case K_F1:
  case K_XBUTTON: /* right-controller A */
    if (m_mods_catalogue) {
      AddonCatalog_Refresh();
      m_mods_confirm = -1;
      S_LocalSound("misc/menu2.wav");
    }
    break;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    if (m_mods_catalogue) {
      int catalogue_index = M_Mods_CatalogueIndex(m_mods_cursor);
      const addon_catalog_entry_t *catalogue =
          AddonCatalog_Entry(catalogue_index);
      if (!catalogue)
        break;
      S_LocalSound("misc/menu2.wav");
      if (catalogue->installed) {
        IN_Activate();
        key_dest = key_game;
        m_state = m_none;
        Cbuf_AddText(va("playgame \"%s\"\n", catalogue->gamedir));
      } else if (m_mods_confirm == m_mods_cursor) {
        if (AddonCatalog_StartInstall(catalogue_index, true))
          m_mods_confirm = -1;
      } else {
        AddonCatalog_StartInstall(catalogue_index, false);
        m_mods_confirm = m_mods_cursor;
      }
    } else {
      item = M_Mods_Item(m_mods_cursor);
      if (item) {
        S_LocalSound("misc/menu2.wav");
        IN_Activate();
        key_dest = key_game;
        m_state = m_none;
        Cbuf_AddText(va("playgame \"%s\"\n", item->name));
      }
    }
    break;
  }
}

static void M_Mods_Char(int key) {
  size_t length;

  if (key < ' ' || key > '~')
    return;

  length = strlen(m_mods_filter);
  if (length >= MODS_FILTER_MAX)
    return;

  m_mods_filter[length] = key;
  m_mods_filter[length + 1] = 0;
  m_mods_cursor = 0;
  m_mods_first = 0;
  m_mods_confirm = -1;
  M_Mods_Refresh();
}

//=============================================================================
/* SERVER-REQUESTED ADD-ON DOWNLOAD */

static int m_servermod_cursor;

void M_Menu_ServerModDownload_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_servermod;
  m_servermod_cursor = 0;
  m_entersound = true;
}

void M_ServerModDownload_Close(void) {
  if (m_state != m_servermod)
    return;
  IN_Activate();
  key_dest = key_game;
  m_state = m_none;
}

static int M_ServerMod_PrintWrapped(int x, int y, const char *text,
                                   int columns, int maxlines) {
  char line[40];
  const char *start, *end, *space;
  int length, lines = 0;

  if (!text)
    return y;
  start = text;
  while (*start && lines < maxlines) {
    while (*start == ' ')
      start++;
    end = start;
    space = NULL;
    while (*end && end - start < columns) {
      if (*end == ' ')
        space = end;
      end++;
    }
    if (*end && space)
      end = space;
    length = q_min((int)sizeof(line) - 1, (int)(end - start));
    memcpy(line, start, length);
    line[length] = 0;
    M_Print(x, y, line);
    y += 8;
    lines++;
    start = end;
  }
  return y;
}

static void M_ServerModDownload_Draw(void) {
  cl_servermod_info_t info;
  char detail[40];
  int action_y;

  if (!CL_ServerModDownload_GetInfo(&info)) {
    M_Menu_Main_f();
    return;
  }

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  M_PrintWhite(112, 8, "SERVER ADD-ON");
  M_DrawTextBox(24, 24, 34, 19);
  M_PrintWhite(40, 36, "The server requires an add-on:");
  if (info.name[0])
    q_snprintf(detail, 31, "%s  (%s)", info.name, info.game);
  else
    q_snprintf(detail, 31, "%s", info.game);
  M_PrintWhite(40, 52, detail);

  if (info.phase == CL_SERVERMOD_PROMPT) {
    if (info.author[0]) {
      q_snprintf(detail, 31, "By %s", info.author);
      M_Print(40, 64, detail);
    }
    q_snprintf(detail, sizeof(detail), "Download size: %.1f MB",
               info.size / (1024.0 * 1024.0));
    M_Print(40, 76, detail);
    M_Print(40, 92, "Source: Ironwail catalogue");
    if (!info.verified)
      M_PrintWhite(40, 108, "Catalogue package has no checksum.");

    action_y = 132;
    M_Print(64, action_y, "Download and Connect");
    M_Print(64, action_y + 16, "Return to Main Menu");
    M_DrawCharacter(48, action_y + m_servermod_cursor * 16,
                    12 + ((int)(realtime * 4) & 1));
  } else {
    action_y = 148;
    if (info.phase == CL_SERVERMOD_INSTALLING) {
      q_snprintf(detail, sizeof(detail), "Downloaded: %.1f MB",
                 AddonCatalog_Progress() / (1024.0 * 1024.0));
      M_PrintWhite(40, 92, "Downloading and installing...");
      M_Print(40, 108, detail);
      M_ServerMod_PrintWrapped(40, 120, info.message, 30, 2);
    } else if (info.phase == CL_SERVERMOD_CHECKING) {
      M_PrintWhite(40, 92, "Checking the add-on catalogue...");
      M_ServerMod_PrintWrapped(40, 108, info.message, 30, 3);
    } else {
      M_PrintWhite(40, 84, "Unable to get the server add-on:");
      M_ServerMod_PrintWrapped(40, 100, info.message, 30, 4);
    }
    M_Print(64, action_y, info.phase == CL_SERVERMOD_INSTALLING
                              ? "Cancel and Return to Menu"
                              : "Return to Main Menu");
    M_DrawCharacter(48, action_y, 12 + ((int)(realtime * 4) & 1));
  }

  if (vr_enabled.value)
    M_Print(40, 180, "Trigger/L-A: select  R-B: return");
  else
    M_Print(56, 180, "Enter: select  Esc: return");
}

static void M_ServerModDownload_ReturnToMain(void) {
  CL_ServerModDownload_Cancel();
  M_Menu_Main_f();
}

static void M_ServerModDownload_Key(int key) {
  cl_servermod_info_t info;

  if (!CL_ServerModDownload_GetInfo(&info)) {
    M_Menu_Main_f();
    return;
  }

  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_ServerModDownload_ReturnToMain();
    return;

  case K_UPARROW:
  case K_DOWNARROW:
  case K_LEFTARROW:
  case K_RIGHTARROW:
  case K_MWHEELUP:
  case K_MWHEELDOWN:
    if (info.phase == CL_SERVERMOD_PROMPT) {
      m_servermod_cursor ^= 1;
      S_LocalSound("misc/menu1.wav");
    }
    return;

  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    if (info.phase == CL_SERVERMOD_PROMPT) {
      S_LocalSound("misc/menu2.wav");
      if (m_servermod_cursor == 0)
        CL_ServerModDownload_Accept();
      else
        M_ServerModDownload_ReturnToMain();
    } else if (info.phase == CL_SERVERMOD_ERROR) {
      S_LocalSound("misc/menu2.wav");
      M_ServerModDownload_ReturnToMain();
    }
    /* Ignore activation repeats while refresh/download work is in flight. */
    return;

  case K_DEL: /* pointer activation of the visible cancel/return action */
    M_ServerModDownload_ReturnToMain();
    return;
  }
}

//=============================================================================
/* MODELS MENU */

static void M_Menu_Models_f(void) {
  IN_Deactivate(modestate == MS_WINDOWED);
  key_dest = key_menu;
  m_state = m_models;
  m_entersound = true;
}

static int m_models_cursor;

static const char *M_HUDStyleName(void) {
  switch (CLAMP(HUD_CLASSIC, (int)scr_hudstyle.value, HUD_COUNT - 1)) {
  case HUD_MODERN_CENTERAMMO:
    return "modern (center ammo)";
  case HUD_MODERN_SIDEAMMO:
    return "modern (side ammo)";
  default:
    return "classic";
  }
}

static void M_Models_Adjust(int direction) {
  if (m_models_cursor == 0) {
    Cvar_SetValue("r_enhancedmodels",
                  Cvar_VariableValue("r_enhancedmodels") ? 0 : 1);
    return;
  }

  Cvar_SetValue("scr_hudstyle",
                (float)((CLAMP(HUD_CLASSIC, (int)scr_hudstyle.value,
                               HUD_COUNT - 1) + HUD_COUNT + direction) %
                        HUD_COUNT));
}

static void M_Models_Draw(void) {
  qboolean enhanced = Cvar_VariableValue("r_enhancedmodels") != 0;

  M_DrawTransPic(16, 4, Draw_CachePic("gfx/qplaque.lmp"));
  M_PrintWhite(120, 8, "MODELS & HUD");
  M_Print(24, 48, "Enhanced model replacements");
  M_Print(24, 64, "Model set");
  M_Print(176, 64, enhanced ? "enhanced" : "classic");
  M_Print(24, 80, "HUD layout");
  M_Print(152, 80, M_HUDStyleName());
  M_Print(24, 104, "Enhanced models use MD3 or MD5");
  M_Print(24, 112, "files when a mod supplies them.");
  M_Print(24, 128, "Modern HUD layouts are desktop only;");
  M_Print(24, 136, "VR and custom mod HUDs stay unchanged.");
  M_Print(24, 152, "Missing models or skins fall back");
  M_Print(24, 160, "to the original Quake model.");
  M_DrawCharacter(160, 64 + m_models_cursor * 16,
                  12 + ((int)(realtime * 4) & 1));
}

static void M_Models_Key(int key) {
  switch (key) {
  case K_ESCAPE:
  case K_BBUTTON:
    M_Menu_Options_f();
    break;

  case K_LEFTARROW:
    S_LocalSound("misc/menu3.wav");
    M_Models_Adjust(-1);
    break;
  case K_RIGHTARROW:
  case K_ENTER:
  case K_KP_ENTER:
  case K_ABUTTON:
    S_LocalSound("misc/menu3.wav");
    M_Models_Adjust(1);
    break;

  case K_UPARROW:
    S_LocalSound("misc/menu1.wav");
    m_models_cursor = (m_models_cursor + 1) % 2;
    break;
  case K_DOWNARROW:
    S_LocalSound("misc/menu1.wav");
    m_models_cursor = (m_models_cursor + 1) % 2;
    break;
  }
}

//=============================================================================
/* Menu Subsystem */

void M_Init(void) {
  Cmd_AddCommand("togglemenu", M_ToggleMenu_f);

  Cmd_AddCommand("menu_main", M_Menu_Main_f);
  Cmd_AddCommand("menu_singleplayer", M_Menu_SinglePlayer_f);
  Cmd_AddCommand("menu_load", M_Menu_Load_f);
  Cmd_AddCommand("menu_save", M_Menu_Save_f);
  Cmd_AddCommand("menu_multiplayer", M_Menu_MultiPlayer_f);
  Cmd_AddCommand("menu_setup", M_Menu_Setup_f);
  Cmd_AddCommand("menu_options", M_Menu_Options_f);
  Cmd_AddCommand("menu_keys", M_Menu_Keys_f);
  Cmd_AddCommand("menu_weapons", M_Menu_Weapons_f);
  Cmd_AddCommand("menu_video", M_Menu_Video_f);
  Cmd_AddCommand("menu_vr", M_Menu_VR_f);
  Cmd_AddCommand("menu_mods", M_Menu_Mods_f);
  Cmd_AddCommand("menu_models", M_Menu_Models_f);
  Cmd_AddCommand("help", M_Menu_Help_f);
  Cmd_AddCommand("menu_quit", M_Menu_Quit_f);
  Cmd_AddCommand("menu_credits",
                 M_Menu_Credits_f); // needed by the 2021 re-release
}

void M_Draw(void) {
  if (m_state == m_none || key_dest != key_menu)
    return;

  if (!m_recursiveDraw) {
    /* A disconnected VR menu is already drawn on its own compact backdrop.
     * The forced console background is opaque and would cover that entire
     * world-space panel once per eye. */
    if (scr_con_current && !(vr_enabled.value && R_IsVRStereoFrame())) {
      Draw_ConsoleBackground();
      S_ExtraUpdate();
    }

    Draw_FadeScreen(); // johnfitz -- fade even if console fills screen
  } else {
    m_recursiveDraw = false;
  }

  GL_SetCanvas(CANVAS_MENU); // johnfitz

  switch (m_state) {
  case m_none:
    break;

  case m_main:
    M_Main_Draw();
    break;

  case m_singleplayer:
    M_SinglePlayer_Draw();
    break;

  case m_load:
    M_Load_Draw();
    break;

  case m_save:
    M_Save_Draw();
    break;

  case m_multiplayer:
    M_MultiPlayer_Draw();
    break;

  case m_setup:
    M_Setup_Draw();
    break;

  case m_net:
    M_Net_Draw();
    break;

  case m_options:
    M_Options_Draw();
    break;

  case m_keys:
    M_Keys_Draw();
    break;

  case m_weapons:
    M_Weapons_Draw();
    break;

  case m_video:
    M_Video_Draw();
    break;

  case m_vr:
    M_VR_Draw();
    break;

  case m_help:
    M_Help_Draw();
    break;

  case m_mods:
    M_Mods_Draw();
    break;

  case m_servermod:
    M_ServerModDownload_Draw();
    break;

  case m_models:
    M_Models_Draw();
    break;

  case m_quit:
    if (!fitzmode) { /* QuakeSpasm customization: */
      /* Quit now! S.A. */
      key_dest = key_console;
      Host_Quit_f();
    }
    M_Quit_Draw();
    break;

  case m_lanconfig:
    M_LanConfig_Draw();
    break;

  case m_gameoptions:
    M_GameOptions_Draw();
    break;

  case m_search:
    M_Search_Draw();
    break;

  case m_slist:
    M_ServerList_Draw();
    break;
  }

  if (m_entersound) {
    S_LocalSound("misc/menu2.wav");
    m_entersound = false;
  }

  S_ExtraUpdate();
}

qboolean M_ConsumesBoundKey(int key) {
  if (m_state == m_servermod)
    return key == K_ABUTTON || key == K_BBUTTON;

  if (m_state != m_mods)
    return false;

  /* These buttons are menu actions here, so own both their press and release
   * even when the user has also bound them to a gameplay +command. */
  if (key == K_ABUTTON)
    return true;

  return m_mods_catalogue && (key == K_F1 || key == K_XBUTTON);
}

void M_Keydown(int key) {
  /* Keyboard/gamepad navigation owns the shared cursor after any key press. */
  if (key != K_MOUSE1) {
    m_pointer_hover = false;
    m_pointer_state = m_none;
  }

  switch (m_state) {
  case m_none:
    return;

  case m_main:
    M_Main_Key(key);
    return;

  case m_singleplayer:
    M_SinglePlayer_Key(key);
    return;

  case m_load:
    M_Load_Key(key);
    return;

  case m_save:
    M_Save_Key(key);
    return;

  case m_multiplayer:
    M_MultiPlayer_Key(key);
    return;

  case m_setup:
    M_Setup_Key(key);
    return;

  case m_net:
    M_Net_Key(key);
    return;

  case m_options:
    M_Options_Key(key);
    return;

  case m_keys:
    M_Keys_Key(key);
    return;

  case m_weapons:
    M_Weapons_Key(key);
    return;

  case m_video:
    M_Video_Key(key);
    return;

  case m_vr:
    M_VR_Key(key);
    break;

  case m_help:
    M_Help_Key(key);
    return;

  case m_mods:
    M_Mods_Key(key);
    return;

  case m_servermod:
    M_ServerModDownload_Key(key);
    return;

  case m_models:
    M_Models_Key(key);
    return;

  case m_quit:
    M_Quit_Key(key);
    return;

  case m_lanconfig:
    M_LanConfig_Key(key);
    return;

  case m_gameoptions:
    M_GameOptions_Key(key);
    return;

  case m_search:
    M_Search_Key(key);
    break;

  case m_slist:
    M_ServerList_Key(key);
    return;
  }
}

void M_Charinput(int key) {
  switch (m_state) {
  case m_setup:
    M_Setup_Char(key);
    return;
  case m_quit:
    M_Quit_Char(key);
    return;
  case m_lanconfig:
    M_LanConfig_Char(key);
    return;
  case m_mods:
    M_Mods_Char(key);
    return;
  default:
    return;
  }
}

qboolean M_TextEntry(void) {
  switch (m_state) {
  case m_setup:
    return M_Setup_TextEntry();
  case m_quit:
    return M_Quit_TextEntry();
  case m_lanconfig:
    return M_LanConfig_TextEntry();
  case m_mods:
    return true;
  default:
    return false;
  }
}

//=============================================================================
/* POINTER INPUT */

/*
 * These helpers deliberately select the existing menu cursors rather than
 * retaining a second UI tree.  The rectangles mirror what is visibly drawn in
 * the classic 320x200 menu canvas, so a pointer outside an actionable row
 * cannot activate whichever row happened to be selected previously.
 */
static qboolean M_PointerRows(float x, float y, float left, float right,
                              float top, float rowheight, int count,
                              int *selection) {
  int row;

  if (x < left || x >= right || y < top || y >= top + rowheight * count)
    return false;

  row = (int)((y - top) / rowheight);
  if (row < 0 || row >= count)
    return false;

  *selection = row;
  return true;
}

static qboolean M_PointerTable(float x, float y, float left, float right,
                               const int *table, int count, int *selection) {
  int i;

  if (x < left || x >= right)
    return false;

  for (i = 0; i < count; i++) {
    if (y >= table[i] && y < table[i] + 8) {
      *selection = i;
      return true;
    }
  }

  return false;
}

static qboolean M_PointerHit(float x, float y) {
  int selection;

  switch (m_state) {
  case m_main:
    if (M_PointerRows(x, y, 64, 264, 32, 20, MAIN_ITEMS, &selection)) {
      m_main_cursor = selection;
      return true;
    }
    break;

  case m_singleplayer:
    if (M_PointerRows(x, y, 64, 264, 32, 20, SINGLEPLAYER_ITEMS,
                      &selection)) {
      m_singleplayer_cursor = selection;
      return true;
    }
    break;

  case m_load:
  case m_save:
    if (M_PointerRows(x, y, 8, 312, 32, 8, MAX_SAVEGAMES, &selection)) {
      load_cursor = selection;
      return true;
    }
    break;

  case m_multiplayer:
    if (M_PointerRows(x, y, 64, 264, 32, 20, MULTIPLAYER_ITEMS,
                      &selection)) {
      m_multiplayer_cursor = selection;
      return true;
    }
    break;

  case m_setup:
    if (M_PointerTable(x, y, 48, 312, setup_cursor_table, NUM_SETUP_CMDS,
                       &selection)) {
      setup_cursor = selection;
      return true;
    }
    break;

  case m_net:
    if (M_PointerRows(x, y, 64, 264, 32, 20, m_net_items, &selection)) {
      m_net_cursor = selection;
      return true;
    }
    break;

  case m_options:
    if (M_PointerRows(x, y, 8, 320, 32, 8, OPTIONS_ITEMS, &selection)) {
      if (!vid_menudrawfn && (selection == OPT_VIDEO || selection == OPT_VR))
        break;
      options_cursor = selection;
      return true;
    }
    break;

  case m_video:
    return vid_menupointerfn && (*vid_menupointerfn)(x, y);

  case m_vr:
    return VR_MenuPointerMove(x, y);

  case m_keys:
    if (M_PointerRows(x, y, 8, 312, 48, 8, NUMCOMMANDS, &selection)) {
      keys_cursor = selection;
      return true;
    }
    break;

  case m_weapons:
    if (M_PointerRows(x, y, 8, 312, 48, 8, NUM_WEAPON_COMMANDS,
                      &selection)) {
      weapons_cursor = selection;
      return true;
    }
    break;

  case m_models:
    if (M_PointerRows(x, y, 8, 312, 56, 16, 2, &selection)) {
      m_models_cursor = selection;
      return true;
    }
    break;

  case m_lanconfig:
    if (M_PointerTable(x, y, 64, 312, lanConfig_cursor_table,
                       StartingGame ? 2 : NUM_LANCONFIG_CMDS, &selection)) {
      lanConfig_cursor = selection;
      return true;
    }
    break;

  case m_gameoptions:
    if (M_PointerTable(x, y, 0, 312, gameoptions_cursor_table,
                       NUM_GAMEOPTIONS, &selection)) {
      gameoptions_cursor = selection;
      return true;
    }
    break;

  case m_slist:
    if (M_PointerRows(x, y, 0, 312, 32, 8, hostCacheCount, &selection)) {
      slist_cursor = selection;
      return true;
    }
    break;

  case m_mods: {
    int visible;

    M_Mods_Refresh();
    visible = q_min(MAX_MODS_ON_SCREEN, m_mods_matches - m_mods_first);
    if (visible > 0 &&
        M_PointerRows(x, y, 32, 280, 64, 8, visible, &selection)) {
      m_mods_cursor = m_mods_first + selection;
      M_Mods_KeepCursorVisible();
      return true;
    }
    if (vr_enabled.value) {
      if (x >= 32 && x < 200 && y >= 184 && y < 192) {
        m_pointer_activate_key = K_TAB;
        return true;
      }
      if (m_mods_catalogue && x >= 208 && x < 280 && y >= 184 && y < 192) {
        m_pointer_activate_key = K_F1;
        return true;
      }
    } else {
      if (x >= 32 && x < 224 && y >= 176 && y < 184) {
        m_pointer_activate_key = K_TAB;
        return true;
      }
      if (m_mods_catalogue && x >= 32 && x < 120 && y >= 184 && y < 192) {
        m_pointer_activate_key = K_F1;
        return true;
      }
    }
    break;
  }

  case m_servermod: {
    cl_servermod_info_t info;
    if (!CL_ServerModDownload_GetInfo(&info))
      break;
    if (info.phase == CL_SERVERMOD_PROMPT) {
      if (M_PointerRows(x, y, 40, 280, 132, 16, 2, &selection)) {
        m_servermod_cursor = selection;
        return true;
      }
    } else if (x >= 40 && x < 280 && y >= 148 && y < 164) {
      m_servermod_cursor = 0;
      m_pointer_activate_key = K_DEL;
      return true;
    }
    break;
  }

  case m_help:
    if (x >= 0 && x < 320 && y >= 0 && y < 200) {
      m_pointer_activate_key = x < 160 ? K_LEFTARROW : K_RIGHTARROW;
      return true;
    }
    break;

  default:
    break;
  }

  return false;
}

void M_PointerMove(float x, float y, m_pointer_source_t source) {
  m_pointer_hover = false;
  m_pointer_state = m_none;
  m_pointer_activate_key = K_ENTER;

  if (key_dest != key_menu || m_state == m_none || bind_grab ||
      x < 0 || x >= 320 || y < 0 || y >= 200)
    return;

  /* cl_mousemenu controls desktop input only; VR remains usable in menus. */
  if (source == M_POINTER_DESKTOP &&
      (!Cvar_VariableValue("cl_mousemenu") || vr_enabled.value))
    return;

  m_pointer_source = source;
  m_pointer_state = m_state;
  m_pointer_hover = M_PointerHit(x, y);
  if (!m_pointer_hover)
    m_pointer_state = m_none;
}

void M_PointerLeave(m_pointer_source_t source) {
  if (m_pointer_hover && m_pointer_source == source) {
    m_pointer_hover = false;
    m_pointer_state = m_none;
    m_pointer_activate_key = K_ENTER;
  }
}

qboolean M_PointerCanActivate(m_pointer_source_t source) {
  return m_pointer_hover && m_pointer_source == source &&
         m_pointer_state == m_state && key_dest == key_menu && !bind_grab;
}

void M_PointerActivate(m_pointer_source_t source) {
  if (!M_PointerCanActivate(source))
    return;

  /* Clear before dispatch because activation commonly changes m_state. */
  m_pointer_hover = false;
  m_pointer_state = m_none;
  M_Keydown(m_pointer_activate_key);
  m_pointer_activate_key = K_ENTER;
}

qboolean M_PointerConsumesMouse(void) {
  return !vr_enabled.value && key_dest == key_menu && m_state != m_none && !bind_grab &&
         Cvar_VariableValue("cl_mousemenu") != 0;
}

qboolean M_BindGrabActive(void) { return bind_grab; }

void M_ConfigureNetSubsystem(void) {
  // enable/disable net systems to match desired config
  Cbuf_AddText("stopdemo\n");

  if (IPXConfig || TCPIPConfig)
    net_hostport = lanConfig_port;
}
