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

// screen.c -- master for refresh, status bar, console, chat, notify, etc

#include "quakedef.h"

#include "cfgfile.h"
#include "vr_input.h"
#include "vr_aim.h"
#include "vr_locomotion.h"
#include "vr_menu_anchor.h"
#include "vr_weapon_menu.h"
#include "r_vrik_render.h"

#include <setjmp.h>

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions


syncronous draw mode or async
One off screen buffer, with updates either copied or xblited
Need to double buffer?


async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint ()
SlowPrint ()
Screen_Update ();
Con_Printf ();

net
turn off messages option

the refresh is allways rendered, unless the console is full screen


console is:
	notify lines
	half
	full

*/

int glwidth, glheight;

float scr_con_current;
float scr_conlines; // lines of console to display

// johnfitz -- new cvars
cvar_t scr_menuscale = {"scr_menuscale", "1", CVAR_ARCHIVE};
cvar_t scr_sbarscale = {"scr_sbarscale", "1", CVAR_ARCHIVE};
cvar_t scr_sbaralpha = {"scr_sbaralpha", "0.75", CVAR_ARCHIVE};
cvar_t scr_conwidth = {"scr_conwidth", "0", CVAR_ARCHIVE};
cvar_t scr_conscale = {"scr_conscale", "1", CVAR_ARCHIVE};
cvar_t scr_crosshairscale = {"scr_crosshairscale", "1", CVAR_ARCHIVE};
cvar_t scr_infoscale = {"scr_infoscale", "2.0", CVAR_ARCHIVE};
cvar_t scr_showfps = {"scr_showfps", "0", CVAR_ARCHIVE};
cvar_t scr_clock = {"scr_clock", "0", CVAR_NONE};
cvar_t scr_autoclock = {"scr_autoclock", "1", CVAR_ARCHIVE};
cvar_t scr_usekfont = {"scr_usekfont", "0", CVAR_NONE}; // 2021 re-release
cvar_t scr_style = {"scr_style", "0", CVAR_ARCHIVE_GAME};
cvar_t vr_menu_scale = {"vr_menu_scale", "0.13", CVAR_ARCHIVE};
cvar_t vr_menu_follow = {"vr_menu_follow", "1", CVAR_ARCHIVE};
cvar_t vr_weaponmenu_mode = {"vr_weaponmenu_mode", "0", CVAR_ARCHIVE};
cvar_t vr_crosshair = {"vr_crosshair", "1", CVAR_ARCHIVE};
cvar_t vr_crosshair_depth = {"vr_crosshair_depth", "0", CVAR_ARCHIVE};
cvar_t vr_crosshair_size = {"vr_crosshair_size", "3", CVAR_ARCHIVE};
cvar_t vr_crosshair_alpha = {"vr_crosshair_alpha", "0.25", CVAR_ARCHIVE};
cvar_t vr_crosshairy = {"vr_crosshairy", "0", CVAR_ARCHIVE};

cvar_t scr_viewsize = {"viewsize", "100", CVAR_ARCHIVE_GAME};
cvar_t scr_viewsize_allow_shrinking = {"viewsize_allow_shrinking", "0", CVAR_ARCHIVE_GAME};
cvar_t scr_fov = {"fov", "90", CVAR_ARCHIVE_GAME}; // 10 - 170
cvar_t scr_fov_adapt = {"fov_adapt", "1", CVAR_ARCHIVE_GAME};
cvar_t scr_zoomfov = {"zoom_fov", "30", CVAR_ARCHIVE_GAME}; // 10 - 170
cvar_t scr_zoomspeed = {"zoom_speed", "8", CVAR_ARCHIVE_GAME};
cvar_t scr_conspeed = {"scr_conspeed", "500", CVAR_ARCHIVE};
cvar_t scr_conanim = {"scr_conanim", "0", CVAR_ARCHIVE};
cvar_t scr_centertime = {"scr_centertime", "2", CVAR_NONE};
cvar_t scr_showturtle = {"showturtle", "0", CVAR_NONE};
cvar_t scr_showpause = {"showpause", "1", CVAR_NONE};
cvar_t scr_printspeed = {"scr_printspeed", "8", CVAR_NONE};
cvar_t scr_centerprintbg = {"scr_centerprintbg", "0", CVAR_ARCHIVE}; // 0=off; 1=classic box; 2=compact box; 3=full-width strip

cvar_t cl_gun_fovscale = {"cl_gun_fovscale", "1", CVAR_ARCHIVE_GAME}; // Qrack
cvar_t cl_gun_x = {"cl_gun_x", "0", CVAR_ARCHIVE_GAME};
cvar_t cl_gun_y = {"cl_gun_y", "0", CVAR_ARCHIVE_GAME};
cvar_t cl_gun_z = {"cl_gun_z", "0", CVAR_ARCHIVE_GAME};

// All scaling is done relative to resolution with scr_relativescale
cvar_t scr_relativescale = {"scr_relativescale", "2", CVAR_ARCHIVE};
cvar_t scr_relmenuscale = {"scr_relmenuscale", "1", CVAR_ARCHIVE};
cvar_t scr_relsbarscale = {"scr_relsbarscale", "1", CVAR_ARCHIVE};
cvar_t scr_relcrosshairscale = {"scr_relcrosshairscale", "1", CVAR_ARCHIVE};
cvar_t scr_relconscale = {"scr_relconscale", "1", CVAR_ARCHIVE};

extern cvar_t vr_aimmode;
extern cvar_t vr_hud_scale;
extern void R_PrepareVRCrosshair (void);
extern qboolean sb_showscores;
extern qboolean scr_drawdialog;

static qboolean scr_vr_classic_sbar_refdef_active;
static THREAD_LOCAL qboolean scr_csqc_display_override_active;
static THREAD_LOCAL csqc_display_t scr_csqc_display_override;

extern cvar_t	 crosshair;
extern cvar_t	 crosshair_def;
extern cvar_t	 crosshair_size;
extern cvar_t	 r_tasks;
extern cvar_t	 r_gpulightmapupdate;
extern cvar_t	 r_showbboxes;
extern cvar_t	 r_showfields;
extern cvar_t	 r_showfields_align;
extern edict_t **bbox_linked;
extern float	 r_fovx;
extern float	 r_fovy;

typedef struct
{
	qboolean valid;
	vec3_t target, right, down, normal;
} scr_vr_field_panel_pose_t;

static scr_vr_field_panel_pose_t vr_field_panel_pose;

qboolean scr_initialized; // ready to draw

qpic_t *scr_net;
qpic_t *scr_turtle;

int clearconsole;

vrect_t scr_vrect;

qboolean		scr_disabled_for_loading;
qboolean		scr_drawloading;
static qboolean scr_drawstartuploading = true;
float			scr_disabled_time;

qboolean	   in_update_screen;
SDL_Mutex	  *draw_qcvm_mutex;

typedef enum
{
	SCR_CSQC_ERROR_IDLE,
	SCR_CSQC_ERROR_ARMED,
	SCR_CSQC_ERROR_CLEANUP
} scr_csqc_error_phase_t;

static THREAD_LOCAL jmp_buf screen_error;
static THREAD_LOCAL scr_csqc_error_phase_t scr_csqc_error_phase;
static THREAD_LOCAL qboolean scr_draw_gui_owns_qc_mutex;

qboolean SCR_CSQCErrorRecoveryArmed (void)
{
	return scr_csqc_error_phase == SCR_CSQC_ERROR_ARMED;
}

void SCR_JumpToCSQCErrorRecovery (void)
{
	if (!SCR_CSQCErrorRecoveryArmed ())
		Sys_Error ("CSQC screen recovery is not armed");
	scr_csqc_error_phase = SCR_CSQC_ERROR_CLEANUP;
	longjmp (screen_error, 1);
}

qboolean SCR_DrawGUIOwnsQCMutex (void)
{
	return scr_draw_gui_owns_qc_mutex;
}

void SCR_ScreenShot_f (void);

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char  scr_centerstring[1024];
float scr_centertime_start; // for slow victory printing
float scr_centertime_off;
float scr_clock_off;
int	  scr_center_lines;
int	  scr_erase_lines;
int	  scr_erase_center;
static int scr_center_maxcols;

void SCR_CenterPrintClear (void)
{
	scr_centertime_off = 0;
	scr_clock_off = 0;
}

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (const char *str) // update centerprint data
{
	COM_WordWrap (scr_centerstring, str, sizeof (scr_centerstring), 40);
	scr_centertime_off = cl.time + scr_centertime.value;
	scr_centertime_start = cl.time;

	// count the number of lines for centering
	scr_center_lines = 1;
	scr_center_maxcols = 0;
	str = scr_centerstring;
	int linecols = 0;
	while (*str)
	{
		if (*str == '\n')
		{
			scr_center_lines++;
			scr_center_maxcols = q_max (scr_center_maxcols, linecols);
			linecols = 0;
		}
		else
			linecols++;
		str++;
	}
	scr_center_maxcols = q_max (scr_center_maxcols, linecols);
}

static void SCR_DrawCenterStringTextBox (cb_context_t *cbx, int x, int y, int width, int lines)
{
	qpic_t *box_tl = Draw_CachePic ("gfx/box_tl.lmp");
	qpic_t *box_ml = Draw_CachePic ("gfx/box_ml.lmp");
	qpic_t *box_bl = Draw_CachePic ("gfx/box_bl.lmp");
	qpic_t *box_tm = Draw_CachePic ("gfx/box_tm.lmp");
	qpic_t *box_mm = Draw_CachePic ("gfx/box_mm.lmp");
	qpic_t *box_mm2 = Draw_CachePic ("gfx/box_mm2.lmp");
	qpic_t *box_bm = Draw_CachePic ("gfx/box_bm.lmp");
	qpic_t *box_tr = Draw_CachePic ("gfx/box_tr.lmp");
	qpic_t *box_mr = Draw_CachePic ("gfx/box_mr.lmp");
	qpic_t *box_br = Draw_CachePic ("gfx/box_br.lmp");
	int cx = x;
	int cy = y;
	int n;

	Draw_Pic (cbx, cx, cy, box_tl, 0.7f, true);
	for (n = 0; n < lines; n++)
	{
		cy += CHARACTER_SIZE;
		Draw_Pic (cbx, cx, cy, box_ml, 0.7f, true);
	}
	Draw_Pic (cbx, cx, cy + CHARACTER_SIZE, box_bl, 0.7f, true);

	cx += CHARACTER_SIZE;
	while (width > 0)
	{
		cy = y;
		Draw_Pic (cbx, cx, cy, box_tm, 0.7f, true);
		for (n = 0; n < lines; n++)
		{
			cy += CHARACTER_SIZE;
			Draw_Pic (cbx, cx, cy, n == 1 ? box_mm2 : box_mm, 0.7f, true);
		}
		Draw_Pic (cbx, cx, cy + CHARACTER_SIZE, box_bm, 0.7f, true);
		width -= 2;
		cx += 2 * CHARACTER_SIZE;
	}

	cy = y;
	Draw_Pic (cbx, cx, cy, box_tr, 0.7f, true);
	for (n = 0; n < lines; n++)
	{
		cy += CHARACTER_SIZE;
		Draw_Pic (cbx, cx, cy, box_mr, 0.7f, true);
	}
	Draw_Pic (cbx, cx, cy + CHARACTER_SIZE, box_br, 0.7f, true);
}

static void SCR_DrawCenterStringBackground (cb_context_t *cbx, int y)
{
	int width, x;

	if (cl.intermission || scr_center_lines <= 0 || scr_center_maxcols <= 0)
		return;

	switch ((int)scr_centerprintbg.value)
	{
	case 1:
		width = (scr_center_maxcols + 3) & ~1;
		x = (320 - width * CHARACTER_SIZE) / 2;
		SCR_DrawCenterStringTextBox (cbx, x - CHARACTER_SIZE, y - 12, width, scr_center_lines + 1);
		break;
	case 2:
		width = q_min (scr_center_maxcols, 40) + 2;
		x = (320 - width * CHARACTER_SIZE) / 2;
		Draw_Fill (cbx, x, y - 4, width * CHARACTER_SIZE, scr_center_lines * CHARACTER_SIZE + 8, 0, 0.7f);
		break;
	case 3:
		Draw_Fill (cbx, 0, y - 4, 320, scr_center_lines * CHARACTER_SIZE + 8, 0, 0.7f);
		break;
	default:
		break;
	}
}

static void SCR_DrawCenterString (cb_context_t *cbx) // actually do the drawing
{
	char *start;
	int	  l;
	int	  j;
	int	  x, y;
	int	  remaining;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	// the finale prints the characters one at a time
	if (cl.intermission)
		remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
	else
		remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

	if (scr_center_lines <= 4)
		y = 200 * 0.35; // johnfitz -- 320x200 coordinate system
	else
		y = 48;
	if (crosshair.value)
		y -= CHARACTER_SIZE;

	SCR_DrawCenterStringBackground (cbx, y);

	do
	{
		// scan the width of the line
		for (l = 0; start[l]; l++)
			if (start[l] == '\n')
				break;
		x = (320 - l * CHARACTER_SIZE) / 2; // johnfitz -- 320x200 coordinate system
		for (j = 0; j < l; j++, x += CHARACTER_SIZE)
		{
			Draw_Character (cbx, x, y, start[j]); // johnfitz -- stretch overlays
			if (!remaining--)
				return;
		}

		y += CHARACTER_SIZE;
		start += l;

		if (!*start)
			break;
		start++; // skip the \n
	} while (1);
}

static void SCR_CheckDrawCenterString (cb_context_t *cbx)
{
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	if (scr_centertime_off <= cl.time && !cl.intermission)
		return;
	if (key_dest != key_game)
		return;
	if (cl.paused && (!cls.demoplayback || cls.demospeed > 0.f)) // johnfitz -- don't show centerprint during a pause
		return;

	SCR_DrawCenterString (cbx);
}

//=============================================================================

/*
====================
SCR_ToggleZoom_f
====================
*/
static void SCR_ToggleZoom_f (void)
{
	if (cl.zoomdir)
		cl.zoomdir = -cl.zoomdir;
	else
		cl.zoomdir = cl.zoom > 0.5f ? -1.f : 1.f;
}

/*
====================
SCR_ZoomDown_f
====================
*/
static void SCR_ZoomDown_f (void)
{
	cl.zoomdir = 1.f;
}

/*
====================
SCR_ZoomUp_f
====================
*/
static void SCR_ZoomUp_f (void)
{
	cl.zoomdir = -1.f;
}

/*
====================
SCR_UpdateZoom
====================
*/
void SCR_UpdateZoom (void)
{
	float delta = cl.zoomdir * scr_zoomspeed.value * (cl.time - cl.oldtime);
	if (!delta)
		return;
	cl.zoom += delta;
	if (cl.zoom >= 1.f)
	{
		cl.zoom = 1.f;
		cl.zoomdir = 0.f;
	}
	else if (cl.zoom <= 0.f)
	{
		cl.zoom = 0.f;
		cl.zoomdir = 0.f;
	}
	vid.recalc_refdef = 1;
}

/*
====================
AdaptFovx
Adapt a 4:3 horizontal FOV to the current screen size using the "Hor+" scaling:
2.0 * atan(width / height * 3.0 / 4.0 * tan(fov_x / 2.0))
====================
*/
static float AdaptFovx (float fov_x, float width, float height)
{
	float a, x;

	if (fov_x < 1 || fov_x > 179)
		Sys_Error ("Bad fov: %f", fov_x);
	if (cl.statsf[STAT_VIEWZOOM])
	{
		fov_x *= cl.statsf[STAT_VIEWZOOM] / 255.0;
		fov_x = CLAMP (1, fov_x, 179);
	}

	if (!scr_fov_adapt.value)
		return fov_x;
	if ((x = height / width) == 0.75)
		return fov_x;
	a = atan (0.75 / x * tan (fov_x / 360 * M_PI));
	a = a * 360 / M_PI;
	return a;
}

/*
====================
CalcFovy
====================
*/
static float CalcFovy (float fov_x, float width, float height)
{
	float a, x;

	if (fov_x < 1 || fov_x > 179)
		Sys_Error ("Bad fov: %f", fov_x);

	x = width / tan (fov_x / 360 * M_PI);
	a = atan (height / x);
	a = a * 360 / M_PI;
	return a;
}

/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
static qboolean SCR_VRClassicSbarFrameEligible (const vrxr_frame_t *frame);

static void SCR_CalcRefdef (void)
{
	float size, scale; // johnfitz -- scale
	float zoom;
	const qboolean vr_classic_sbar_panel = SCR_VRClassicSbarFrameEligible (GL_OpenXRFrame ());

	// bound viewsize
	if (scr_viewsize.value < 30)
		Cvar_SetQuick (&scr_viewsize, "30");
	if (scr_viewsize.value > 130)
		Cvar_SetQuick (&scr_viewsize, "130");

	// bound fov
	if (scr_fov.value < 10)
		Cvar_SetQuick (&scr_fov, "10");
	if (scr_fov.value > 170)
		Cvar_SetQuick (&scr_fov, "170");
	if (scr_zoomfov.value < 10)
		Cvar_SetQuick (&scr_zoomfov, "10");
	if (scr_zoomfov.value > 170)
		Cvar_SetQuick (&scr_zoomfov, "170");

	vid.recalc_refdef = 0;

	// johnfitz -- rewrote this section
	size = scr_viewsize.value;
	scale = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);

	if ((size >= 120) || cl.intermission || (scr_sbaralpha.value < 1) || ((scr_style.value < 1.0f) && cl.qcvm.extfuncs.CSQC_DrawHud) ||
		(scr_style.value >= 2.0f)) // johnfitz -- scr_sbaralpha.value. Spike -- simple csqc assumes fullscreen video the same way.
		sb_lines = 0;
	else if (size >= 110)
		sb_lines = 24 * scale;
	else
		sb_lines = 48 * scale;
	if (vr_classic_sbar_panel)
		sb_lines = 0;
	scr_vr_classic_sbar_refdef_active = vr_classic_sbar_panel;

	size = q_min (scr_viewsize.value, 100.f) / 100;
	// johnfitz

	// johnfitz -- rewrote this section
	r_refdef.vrect.width = q_max (glwidth * size, 96);					  // no smaller than 96, for icons
	r_refdef.vrect.height = q_min (glheight * size, glheight - sb_lines); // make room for sbar
	r_refdef.vrect.x = (glwidth - r_refdef.vrect.width) / 2;
	r_refdef.vrect.y = (glheight - sb_lines - r_refdef.vrect.height) / 2;
	// johnfitz

	zoom = cl.zoom;
	zoom *= zoom * (3.f - 2.f * zoom); // smoothstep
	r_refdef.basefov = scr_fov.value + (scr_zoomfov.value - scr_fov.value) * zoom;
	r_refdef.fov_x = AdaptFovx (r_refdef.basefov, vid.width, vid.height);
	r_refdef.fov_y = CalcFovy (r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height);

	scr_vrect = r_refdef.vrect;
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
static void SCR_SizeUp_f (void)
{
	Cvar_SetValueQuick (&scr_viewsize, scr_viewsize.value + 10);
}

/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void SCR_SizeDown_f (void)
{
	float new_value = scr_viewsize.value - 10;
	if (!scr_viewsize_allow_shrinking.value)
		new_value = q_max (new_value, 100);
	Cvar_SetValueQuick (&scr_viewsize, new_value);
}

/* All HUD styles use one tracked pose and one set of live-game restrictions. */
static qboolean SCR_VRHUDFrameEligible (const vrxr_frame_t *frame)
{
	const qboolean disconnected_loading = scr_drawstartuploading && cls.state == ca_disconnected;
	const qboolean in_game_menu = key_dest == key_menu && m_state != m_none;
	const qboolean live_game = key_dest == key_game && m_state == m_none && !scr_drawdialog;
	vec3_t angles;
	int dominant;

	if (!vulkan_globals.stereo_active || !frame || !frame->should_render || !frame->focused ||
		!frame->devices[0].valid || !frame->devices[0].tracked || frame->devices[0].kind != VRXR_DEVICE_HEAD ||
		frame->devices[0].hand != -1 || cls.signon != SIGNONS || !cl.worldmodel || con_forcedup ||
		(!live_game && !in_game_menu && !scr_drawdialog) || scr_con_current > 0 || disconnected_loading ||
		cl.intermission ||
		!isfinite (vr_aimmode.value) || !isfinite (vr_hud_scale.value) || vr_hud_scale.value <= 0)
		return false;
	for (int row = 0; row < 3; ++row)
		for (int column = 0; column < 4; ++column)
			if (!isfinite (frame->devices[0].matrix[row][column]))
				return false;

	if (vr_aimmode.value == VR_AIMMODE_CONTROLLER)
	{
		dominant = VR_InputDominantPhysicalHand ();
		if (dominant < 0 || dominant > 1 || !frame->devices[dominant + 1].valid ||
			!frame->devices[dominant + 1].tracked || frame->devices[dominant + 1].kind != VRXR_DEVICE_HAND ||
			frame->devices[dominant + 1].hand != dominant ||
			!V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, dominant, angles))
			return false;
	}
	else
	{
		VectorCopy (cl.viewangles, angles);
		for (int i = 0; i < 3; ++i)
			if (!isfinite (cl.viewent.origin[i]))
				return false;
	}

	for (int i = 0; i < 3; ++i)
		if (!isfinite (angles[i]))
			return false;
	return true;
}

/* Only the classic/CSQC panel replaces status-bar scene reservation. */
static qboolean SCR_VRClassicSbarFrameEligible (const vrxr_frame_t *frame)
{
	const qboolean csqc_hud = scr_style.value < 1.0f && cl.qcvm.extfuncs.CSQC_DrawHud;

	return SCR_VRHUDFrameEligible (frame) && isfinite (scr_style.value) && scr_style.value < 2.0f &&
		!(csqc_hud && qcvm);
}

static qboolean SCR_VRModernSbarFrameEligible (const vrxr_frame_t *frame)
{
	/* Modern score/death uses CANVAS_SBAR inside Sbar_DrawModern. */
	return SCR_VRHUDFrameEligible (frame) && isfinite (scr_style.value) && scr_style.value >= 2.0f &&
		isfinite (scr_viewsize.value) && scr_viewsize.value < 120.0f;
}

static void SCR_Callback_refdef (cvar_t *var)
{
	vid.recalc_refdef = 1;
}

/*
==================
SCR_Conwidth_f -- johnfitz -- called when scr_conwidth or scr_conscale changes
==================
*/
static void SCR_Conwidth_f (cvar_t *var)
{
	vid.recalc_refdef = 1;
	vid.conwidth = (scr_conwidth.value > 0) ? (int)scr_conwidth.value : (scr_conscale.value > 0) ? (int)(vid.width / scr_conscale.value) : vid.width;
	vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
}

static float SCR_GetRelativeScale (void)
{
	return CLAMP (1.0f, scr_relativescale.value, 3.0f) * 0.0013f;
}

/*
==================
SCR_GetCSQCDisplay
==================
*/
csqc_display_t SCR_GetCSQCDisplay (void)
{
	csqc_display_t display;
	if (scr_csqc_display_override_active)
		return scr_csqc_display_override;
	if (scr_relativescale.value && vid.width > 0 && vid.height > 0)
	{
		// Some mods' HUDs assume resolutions around 1080p and lay out incorrectly at higher resolutions.
		// Give them a roughly 1080p virtual display (two million pixels) at the actual aspect ratio,
		// then scale their drawing to the framebuffer. Absolute scaling retains the original behavior.
		float aspect = (float)vid.width / (float)vid.height;
		display.width = (int)roundf (sqrtf (2000000.0f * aspect));
		display.height = (int)roundf (sqrtf (2000000.0f / aspect));
		float scale = display.height * scr_relsbarscale.value * SCR_GetRelativeScale ();
		display.scale = CLAMP (1.0, scale, display.width / 320.0);
		display.pixel_scale[0] = display.scale * glwidth / display.width;
		display.pixel_scale[1] = display.scale * glheight / display.height;
	}
	else
	{
		display.width = vid.width;
		display.height = vid.height;
		display.scale = CLAMP (1.0, scr_sbarscale.value, (float)glwidth / 320.0);
		display.pixel_scale[0] = display.pixel_scale[1] = display.scale;
	}
	return display;
}

void SCR_SetCSQCDisplayOverride (const csqc_display_t *display)
{
	if (display)
	{
		scr_csqc_display_override = *display;
		scr_csqc_display_override_active = true;
	}
	else
		scr_csqc_display_override_active = false;
}

qboolean SCR_CSQCDisplayOverrideActive (void)
{
	return scr_csqc_display_override_active;
}

/*
==================
SCR_UpdateRelativeScale
==================
*/
void SCR_UpdateRelativeScale ()
{
	if (scr_relativescale.value)
	{
		float relative_scale = SCR_GetRelativeScale ();

		scr_menuscale.flags &= ~(CVAR_ARCHIVE | CVAR_ROM);
		Cvar_SetValue ("scr_menuscale", (float)vid.height * scr_relmenuscale.value * relative_scale);
		scr_menuscale.flags |= CVAR_ROM;

		scr_sbarscale.flags &= ~(CVAR_ARCHIVE | CVAR_ROM);
		Cvar_SetValue ("scr_sbarscale", (float)vid.height * scr_relsbarscale.value * relative_scale);
		scr_sbarscale.flags |= CVAR_ROM;

		scr_crosshairscale.flags &= ~(CVAR_ARCHIVE | CVAR_ROM);
		Cvar_SetValue ("scr_crosshairscale", (float)vid.height * scr_relcrosshairscale.value * relative_scale);
		scr_crosshairscale.flags |= CVAR_ROM;

		scr_conscale.flags &= ~(CVAR_ARCHIVE | CVAR_ROM);
		Cvar_SetValue ("scr_conscale", (float)vid.height * scr_relconscale.value * relative_scale);
		scr_conscale.flags |= CVAR_ROM;
	}
	else
	{
		scr_menuscale.flags |= CVAR_ARCHIVE;
		scr_menuscale.flags &= ~CVAR_ROM;
		scr_sbarscale.flags |= CVAR_ARCHIVE;
		scr_sbarscale.flags &= ~CVAR_ROM;
		scr_crosshairscale.flags |= CVAR_ARCHIVE;
		scr_crosshairscale.flags &= ~CVAR_ROM;
		scr_conscale.flags |= CVAR_ARCHIVE;
		scr_conscale.flags &= ~CVAR_ROM;
	}
	SCR_Conwidth_f (NULL);
	PR_RefreshCSQCDisplay ();
}

/*
==================
SCR_UpdateRelativeScale_f
==================
*/
static void SCR_UpdateRelativeScale_f (cvar_t *var)
{
	SCR_UpdateRelativeScale ();
}

//============================================================================

/*
==================
SCR_LoadPics -- johnfitz
==================
*/
void SCR_LoadPics (void)
{
	scr_net = Draw_PicFromWad ("net");
	scr_turtle = Draw_PicFromWad ("turtle");
}

/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{
	// johnfitz -- new cvars
	Cvar_RegisterVariable (&scr_menuscale);
	Cvar_RegisterVariable (&scr_sbarscale);
	Cvar_SetCallback (&scr_sbaralpha, SCR_Callback_refdef);
	Cvar_RegisterVariable (&scr_sbaralpha);
	Cvar_SetCallback (&scr_conwidth, &SCR_Conwidth_f);
	Cvar_SetCallback (&scr_conscale, &SCR_Conwidth_f);
	Cvar_RegisterVariable (&scr_conwidth);
	Cvar_RegisterVariable (&scr_conscale);
	Cvar_RegisterVariable (&scr_crosshairscale);
	Cvar_RegisterVariable (&scr_infoscale);
	Cvar_RegisterVariable (&scr_showfps);
	Cvar_RegisterVariable (&scr_clock);
	Cvar_RegisterVariable (&scr_autoclock);
	// johnfitz
	Cvar_RegisterVariable (&scr_usekfont); // 2021 re-release
	Cvar_SetCallback (&scr_fov, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_fov_adapt, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_zoomfov, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_viewsize, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_style, SCR_Callback_refdef);
	Cvar_RegisterVariable (&scr_fov);
	Cvar_RegisterVariable (&scr_fov_adapt);
	Cvar_RegisterVariable (&scr_zoomfov);
	Cvar_RegisterVariable (&scr_zoomspeed);
	Cvar_RegisterVariable (&scr_viewsize);
	Cvar_RegisterVariable (&scr_viewsize_allow_shrinking);
	Cvar_RegisterVariable (&scr_conspeed);
	Cvar_RegisterVariable (&scr_conanim);
	Cvar_RegisterVariable (&scr_showturtle);
	Cvar_RegisterVariable (&scr_showpause);
	Cvar_RegisterVariable (&scr_centertime);
	Cvar_RegisterVariable (&scr_printspeed);
	Cvar_RegisterVariable (&scr_centerprintbg);
	Cvar_RegisterVariable (&scr_style);
	Cvar_RegisterVariable (&vr_menu_scale);
	Cvar_RegisterVariable (&vr_menu_follow);
	Cvar_RegisterVariable (&vr_weaponmenu_mode);
	Cvar_RegisterVariable (&vr_crosshair);
	Cvar_RegisterVariable (&vr_crosshair_depth);
	Cvar_RegisterVariable (&vr_crosshair_size);
	Cvar_RegisterVariable (&vr_crosshair_alpha);
	Cvar_RegisterVariable (&vr_crosshairy);
	Cvar_RegisterVariable (&cl_gun_fovscale);
	Cvar_RegisterVariable (&cl_gun_x);
	Cvar_RegisterVariable (&cl_gun_y);
	Cvar_RegisterVariable (&cl_gun_z);

	Cvar_RegisterVariable (&scr_relativescale);
	Cvar_RegisterVariable (&scr_relmenuscale);
	Cvar_RegisterVariable (&scr_relsbarscale);
	Cvar_RegisterVariable (&scr_relcrosshairscale);
	Cvar_RegisterVariable (&scr_relconscale);
	Cvar_SetCallback (&scr_relativescale, &SCR_UpdateRelativeScale_f);
	Cvar_SetCallback (&scr_relmenuscale, &SCR_UpdateRelativeScale_f);
	Cvar_SetCallback (&scr_relsbarscale, &SCR_UpdateRelativeScale_f);
	Cvar_SetCallback (&scr_relcrosshairscale, &SCR_UpdateRelativeScale_f);
	Cvar_SetCallback (&scr_relconscale, &SCR_UpdateRelativeScale_f);
	SCR_UpdateRelativeScale ();

	if (CFG_OpenConfig (CONFIG_NAME) == 0)
	{
		const char *early_read[] = {"scr_relativescale"};
		CFG_ReadCvars (early_read, 1);
		CFG_CloseConfig ();
	}

	Cmd_AddCommand ("screenshot", SCR_ScreenShot_f);
	Cmd_AddCommand ("sizeup", SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown", SCR_SizeDown_f);

	Cmd_AddCommand ("togglezoom", SCR_ToggleZoom_f);
	Cmd_AddCommand ("+zoom", SCR_ZoomDown_f);
	Cmd_AddCommand ("-zoom", SCR_ZoomUp_f);

	SCR_LoadPics (); // johnfitz

	draw_qcvm_mutex = SDL_CreateMutex ();

	scr_initialized = true;
}

//============================================================================

/*
==============
SCR_DrawFPS -- johnfitz
==============
*/
static void SCR_DrawFPS (cb_context_t *cbx)
{
	static double oldtime = 0;
	static double lastfps = 0;
	static int	  oldframecount = 0;
	double		  elapsed_time;
	int			  frames;

	elapsed_time = realtime - oldtime;
	frames = host_framecount - oldframecount;

	if (elapsed_time < 0 || frames < 0)
	{
		oldtime = realtime;
		oldframecount = host_framecount;
		return;
	}
	// update value every 3/4 second
	if (elapsed_time > 0.75)
	{
		lastfps = frames / elapsed_time;
		oldtime = realtime;
		oldframecount = host_framecount;
	}

	if (scr_showfps.value && scr_viewsize.value < 130)
	{
		char st[16];
		int	 x, y;
		q_snprintf (st, sizeof (st), "%4.0f fps", lastfps);
		x = 320 - (strlen (st) << 3);
		y = 200 - CHARACTER_SIZE;
		GL_SetCanvas (cbx, CANVAS_BOTTOMRIGHT);
		Draw_String (cbx, x, y, st);
	}
}

/*
==============
SCR_DrawSpeeds -- scr_speeds overlay in the top right corner
==============
*/
static void SCR_DrawSpeeds (cb_context_t *cbx)
{
	if (!scr_speeds.value || (rs_display_numlines == 0) || (scr_viewsize.value >= 130))
		return;

	GL_SetCanvas (cbx, CANVAS_TOPRIGHT);
	int y = 0;
	for (int i = 0; i < rs_display_numlines; i++)
	{
		Draw_String (cbx, 320 - ((int)strlen (rs_display_lines[i]) << 3), y, rs_display_lines[i]);
		y += CHARACTER_SIZE;
	}
}

/*
==============
SCR_DrawClock -- johnfitz
==============
*/
static void SCR_DrawClock (cb_context_t *cbx)
{
	char			str[32];
	int				y = 200 - CHARACTER_SIZE;
	static qboolean shown_pause;
	extern qboolean sb_showscores;

	if (cls.demoplayback && cls.demospeed != 1 && cls.demospeed != 0) // always show if playback speed is modified
	{
		scr_clock_off = 2.0f;
		shown_pause = false;
	}

	if (cls.demoplayback && cls.demospeed == 0 && !shown_pause) // show for a bit if paused
	{
		scr_clock_off = 1.5f;
		shown_pause = true;
	}

	if ((scr_clock.value == 0 && scr_clock_off <= 0 && !(sb_showscores && scr_autoclock.value)) || scr_viewsize.value >= 130)
		return;

	scr_clock_off -= host_frametime / (cls.demospeed ? cls.demospeed : 1.f);

	GL_SetCanvas (cbx, CANVAS_BOTTOMRIGHT);

	if (scr_showfps.value)
		y -= CHARACTER_SIZE; // make room for fps counter

	if (scr_clock.value >= 2)
	{
		q_snprintf (str, sizeof (str), "%i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]);
		Draw_String (cbx, 320 - (strlen (str) << 3), y, str);
		y -= CHARACTER_SIZE;
		q_snprintf (str, sizeof (str), "%i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]);
		Draw_String (cbx, 320 - (strlen (str) << 3), y, str);
		y -= CHARACTER_SIZE;
	}

	q_snprintf (str, sizeof (str), "%i:%02i", (int)cl.time / 60, (int)cl.time % 60);
	Draw_String (cbx, 320 - (strlen (str) << 3), y, str);

	// show playback rate
	if (cls.demoplayback && cls.demospeed != 1)
	{
		y -= CHARACTER_SIZE;
		q_snprintf (str, sizeof (str), "[%gx]", cls.demospeed);
		if (cls.demospeed == 0)
			q_snprintf (str, sizeof (str), "[paused]");
		Draw_String (cbx, 320 - (strlen (str) << 3), y, str);
	}
}

typedef struct scr_info_line_s
{
	char key[64];
	char value[256];
} scr_info_line_t;

static qboolean SCR_ProjectWorldToScreen (const vec3_t point, float *x, float *y)
{
	vec3_t delta;
	float  z, px, py;

	VectorSubtract (point, r_origin, delta);
	z = DotProduct (delta, vpn);
	if (z <= 1.0f)
		return false;

	px = DotProduct (delta, vright) / (z * tanf (DEG2RAD (r_fovx) * 0.5f));
	py = DotProduct (delta, vup) / (z * tanf (DEG2RAD (r_fovy) * 0.5f));

	*x = r_refdef.vrect.x + (0.5f + 0.5f * px) * r_refdef.vrect.width;
	*y = r_refdef.vrect.y + (0.5f - 0.5f * py) * r_refdef.vrect.height;
	*x = CLAMP (0.0f, *x, (float)glwidth);
	*y = CLAMP (0.0f, *y, (float)glheight);
	return true;
}

static void SCR_GetEdictCenter (const edict_t *ed, vec3_t center)
{
	VectorCopy (ed->v.origin, center);
	if (!VectorCompare (ed->v.mins, ed->v.maxs))
	{
		VectorMA (center, 0.5f, ed->v.mins, center);
		VectorMA (center, 0.5f, ed->v.maxs, center);
	}
}

static void SCR_GetEdictBottom (const edict_t *ed, vec3_t bottom)
{
	SCR_GetEdictCenter (ed, bottom);
	if (!VectorCompare (ed->v.mins, ed->v.maxs))
		bottom[2] = ed->v.origin[2] + ed->v.mins[2];
}

static void SCR_SetInfoColor (vec3_t color, float r, float g, float b)
{
	color[0] = r;
	color[1] = g;
	color[2] = b;
}

static void SCR_InfoLine (scr_info_line_t *lines, int *numlines, const char *key, const char *value)
{
	if (*numlines >= 96)
		return;

	q_strlcpy (lines[*numlines].key, key ? key : "", sizeof (lines[*numlines].key));
	q_strlcpy (lines[*numlines].value, value ? value : "", sizeof (lines[*numlines].value));
	(*numlines)++;
}

static void SCR_InfoFieldLines (scr_info_line_t *lines, int *numlines, const char *key, const char *value)
{
	const char *start = value;
	char		line[256];

	if (!start || !*start)
	{
		SCR_InfoLine (lines, numlines, key, "");
		return;
	}

	while (*start)
	{
		const char *end = strchr (start, '\n');
		size_t		len = end ? (size_t)(end - start) : strlen (start);
		len = q_min (len, sizeof (line) - 1);
		memcpy (line, start, len);
		line[len] = 0;
		SCR_InfoLine (lines, numlines, key, line);
		key = "";
		if (!end)
			break;
		start = end + 1;
	}
}

static void SCR_DrawInfoPanel (cb_context_t *cbx, float x, float y,
	const scr_info_line_t *lines, int numlines, const vec3_t bgcolor, qboolean physical_table)
{
	float scale, charw, charh, keyw, valuew, width, height;
	const qboolean use_physical_panel = physical_table && vulkan_globals.stereo_active;

	if (numlines <= 0)
		return;

	scale = CLAMP (1.0f, scr_infoscale.value, 8.0f);
	charw = CHARACTER_SIZE * scale;
	charh = CHARACTER_SIZE * scale;
	keyw = valuew = 0.0f;

	for (int i = 0; i < numlines; i++)
	{
		keyw = q_max (keyw, strlen (lines[i].key) * charw);
		valuew = q_max (valuew, strlen (lines[i].value) * charw);
	}

	width = keyw + valuew + 3.0f * charw;
	height = (numlines + 1) * charh;
	x = CLAMP (0.0f, x, q_max (0.0f, glwidth - width));
	y = CLAMP (0.0f, y, q_max (0.0f, glheight - height));

	if (use_physical_panel)
	{
		float world_from_ndc[16] = {0};
		const float hud_scale = vr_hud_scale.value;
		float center_x, bottom_y;

		if (!vr_field_panel_pose.valid || !isfinite (x) || !isfinite (y) ||
			!isfinite (width) || !isfinite (height) || width <= 0.0f || height <= 0.0f ||
			!isfinite (hud_scale) || hud_scale <= 0.0f ||
			vid.width <= 0 || vid.height <= 0 || glwidth <= 0 || glheight <= 0)
			return;
		center_x = 2.0f * (x + width * 0.5f) / vid.width - 1.0f;
		bottom_y = 2.0f * (vid.height - glheight + y + height) / vid.height - 1.0f;
		if (!isfinite (center_x) || !isfinite (bottom_y))
			return;

		/* CANVAS_DEFAULT NDC advances by 2/vid.width and 2/vid.height per
		 * source pixel; its viewport begins at vid.height-glheight vertically. */
		for (int i = 0; i < 3; ++i)
		{
			world_from_ndc[i] = vr_field_panel_pose.right[i] * (hud_scale * vid.width * 0.5f);
			world_from_ndc[4 + i] = vr_field_panel_pose.down[i] * (hud_scale * vid.height * 0.5f);
			world_from_ndc[8 + i] = vr_field_panel_pose.normal[i] * hud_scale;
			world_from_ndc[12 + i] = vr_field_panel_pose.target[i] -
				world_from_ndc[i] * center_x - world_from_ndc[4 + i] * bottom_y;
			if (!isfinite (world_from_ndc[i]) || !isfinite (world_from_ndc[4 + i]) ||
				!isfinite (world_from_ndc[8 + i]) || !isfinite (world_from_ndc[12 + i]))
				return;
		}
		world_from_ndc[15] = 1.0f;
		GL_BeginUIPanel (cbx, world_from_ndc);
	}

	GL_SetCanvas (cbx, CANVAS_DEFAULT);
	Draw_Fill (cbx, x, y, width, height, 0, 0.65f);

	for (int i = 0; i < numlines; i++)
	{
		const float liney = y + (i + 0.5f) * charh;
		GL_SetCanvasColor (0.85f + bgcolor[0] * 0.5f, 0.75f + bgcolor[1] * 0.5f, 0.45f + bgcolor[2] * 0.5f, 1.0f);
		Draw_String_Scaled (cbx, x + 0.5f * charw, liney, lines[i].key, scale);
		GL_SetCanvasColor (1.0f, 1.0f, 1.0f, 1.0f);
		Draw_String_Scaled (cbx, x + keyw + 1.5f * charw, liney, lines[i].value, scale);
	}
	if (use_physical_panel)
		GL_EndUIPanel (cbx);
	GL_SetCanvasColor (1.0f, 1.0f, 1.0f, 1.0f);
}

static void SCR_DrawEdictInfo (cb_context_t *cbx)
{
	scr_info_line_t lines[96];
	vec3_t			anchor, bgcolor;
	float			x, y;
	int				numlines;

	if (VEC_SIZE (bbox_linked) == 0 && VEC_SIZE (r_pointfile) == 0)
		return;

	if (VEC_SIZE (r_pointfile) != 0 && SCR_ProjectWorldToScreen (r_pointfile[0], &x, &y))
	{
		numlines = 0;
		SCR_InfoLine (lines, &numlines, "", "Leak");
		SCR_SetInfoColor (bgcolor, 0.25f, 0.0f, 0.0f);
		SCR_DrawInfoPanel (cbx, x, y, lines, numlines, bgcolor, false);
	}

	if (VEC_SIZE (bbox_linked) == 0)
		return;

	PR_SwitchQCVM (&sv.qcvm);

	for (int i = (int)VEC_SIZE (bbox_linked) - 1; i >= 0; i--)
	{
		edict_t *ed = bbox_linked[i];

		if (i == 0 && r_showfields.value && !r_showfields_align.value)
			continue;

		SCR_GetEdictCenter (ed, anchor);
		if (!SCR_ProjectWorldToScreen (anchor, &x, &y))
			continue;

		numlines = 0;
		SCR_InfoLine (lines, &numlines, "", va ("edict %d", NUM_FOR_EDICT (ed)));
		if (ed->v.classname)
			SCR_InfoLine (lines, &numlines, "", PR_GetString (ed->v.classname));

		switch (ed->showbboxflags)
		{
		default:
		case SHOWBBOX_LINK_NONE:
			SCR_SetInfoColor (bgcolor, 0.0f, 0.0f, 0.0f);
			break;
		case SHOWBBOX_LINK_INCOMING:
			SCR_SetInfoColor (bgcolor, 0.25f, 0.125f, 0.125f);
			break;
		case SHOWBBOX_LINK_OUTGOING:
			SCR_SetInfoColor (bgcolor, 0.125f, 0.125f, 0.25f);
			break;
		case SHOWBBOX_LINK_BOTH:
			SCR_SetInfoColor (bgcolor, 0.25f, 0.125f, 0.25f);
			break;
		}

		SCR_DrawInfoPanel (cbx, x, y, lines, numlines, bgcolor, false);
	}

	if (r_showfields.value)
	{
		edict_t *ed = bbox_linked[0];

		SCR_GetEdictBottom (ed, anchor);
		if (!SCR_ProjectWorldToScreen (anchor, &x, &y) || r_showfields_align.value)
		{
			x = glwidth;
			y = glheight;
		}

		numlines = 0;
		SCR_InfoLine (lines, &numlines, "Edict", va ("%d", NUM_FOR_EDICT (ed)));
		SCR_InfoLine (lines, &numlines, "classname", ed->v.classname ? PR_GetString (ed->v.classname) : "");

		for (int i = 1; i < qcvm->progs->numfielddefs; i++)
		{
			ddef_t *d = &qcvm->fielddefs[i];
			if (d->ofs * 4 == offsetof (entvars_t, classname) || !ED_IsRelevantField (ed, d))
				continue;
			SCR_InfoFieldLines (lines, &numlines, PR_GetString (d->s_name), ED_FieldValueString (ed, d));
		}

		SCR_SetInfoColor (bgcolor, 0.0f, 0.0f, 0.0f);
		SCR_DrawInfoPanel (cbx, x, y, lines, numlines, bgcolor, true);
	}

	PR_SwitchQCVM (NULL);
}

/*
==============
SCR_DrawDevStats
==============
 */
static void SCR_DrawDevStats (cb_context_t *cbx)
{
	char str[40];
	int	 y = 25 - 9; // 9=number of lines to print
	int	 x = 0;		 // margin

	if (!devstats.value)
		return;

	GL_SetCanvas (cbx, CANVAS_BOTTOMLEFT);

	Draw_Fill (cbx, x, y * CHARACTER_SIZE, 19 * CHARACTER_SIZE, 9 * CHARACTER_SIZE, 0, 0.5); // dark rectangle

	q_snprintf (str, sizeof (str), "devstats |Curr Peak");
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);

	q_snprintf (str, sizeof (str), "---------+---------");
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);

	q_snprintf (str, sizeof (str), "Edicts   |%4i %4i", dev_stats.edicts, dev_peakstats.edicts);
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);

	q_snprintf (str, sizeof (str), "Packet   |%4i %4i", dev_stats.packetsize, dev_peakstats.packetsize);
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);

	q_snprintf (str, sizeof (str), "Visedicts|%4i %4i", dev_stats.visedicts, dev_peakstats.visedicts);
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);

	q_snprintf (str, sizeof (str), "Efrags   |%4i %4i", dev_stats.efrags, dev_peakstats.efrags);
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);

	q_snprintf (str, sizeof (str), "Dlights  |%4i %4i", dev_stats.dlights, dev_peakstats.dlights);
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);

	q_snprintf (str, sizeof (str), "Beams    |%4i %4i", dev_stats.beams, dev_peakstats.beams);
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);

	q_snprintf (str, sizeof (str), "Tempents |%4i %4i", dev_stats.tempents, dev_peakstats.tempents);
	Draw_String (cbx, x, (y++) * CHARACTER_SIZE - x, str);
}

/*
==============
SCR_DrawTurtle
==============
*/
static void SCR_DrawTurtle (cb_context_t *cbx)
{
	static int count;

	if (!scr_showturtle.value)
		return;

	if (host_frametime < 0.1)
	{
		count = 0;
		return;
	}

	count++;
	if (count < 3)
		return;

	GL_SetCanvas (cbx, CANVAS_DEFAULT); // johnfitz

	Draw_Pic (cbx, scr_vrect.x, scr_vrect.y, scr_turtle, 1.0f, false);
}

/*
==============
SCR_DrawNet
==============
*/
static void SCR_DrawNet (cb_context_t *cbx)
{
	if (realtime - cl.last_received_message < 0.3)
		return;
	if (cls.demoplayback)
		return;

	GL_SetCanvas (cbx, CANVAS_DEFAULT); // johnfitz

	Draw_Pic (cbx, scr_vrect.x + 64, scr_vrect.y, scr_net, 1.0f, false);
}

/*
==============
DrawPause
==============
*/
static void SCR_DrawPause (cb_context_t *cbx)
{
	qpic_t *pic;

	if (!cl.paused)
		return;

	if (!scr_showpause.value || scr_viewsize.value >= 130) // turn off for screenshots
		return;

	if (cls.demoplayback && cls.demospeed == 0.f)
		return;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/pause.lmp");
	Draw_Pic (cbx, (320 - pic->width) / 2, (240 - 48 - pic->height) / 2, pic, 1.0f, false); // johnfitz -- stretched menus
}

/*
=============
SCR_DrawLoadingPic
=============
*/
static void SCR_DrawLoadingPic (cb_context_t *cbx)
{
	qpic_t *pic;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/loading.lmp");
	Draw_Pic (cbx, (320 - pic->width) / 2, (240 - 48 - pic->height) / 2, pic, 1.0f, false); // johnfitz -- stretched menus
}

/*
=============
SCR_DrawStartupSplashPic
=============
*/
static void SCR_DrawStartupSplashPic (cb_context_t *cbx)
{
	const float startup_logo_scale = 1.5f;
	qpic_t	   *pic;
	float		logo_size, source_size, logo_t;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/qplaque.lmp");
	source_size = pic->width;
	logo_size = source_size * startup_logo_scale;
	logo_t = (pic->height - source_size) / pic->height;
	Draw_SubPic (cbx, (320 - logo_size) / 2, (240 - 48 - logo_size) / 2, logo_size, logo_size, pic, 0, logo_t, 1, source_size / pic->height, NULL, 1.0f);
}

/*
=============
SCR_DrawMenuLoading
=============
*/
static void SCR_DrawMenuLoading (cb_context_t *cbx, qboolean startup)
{
	const float	   old_con_current = scr_con_current;
	const qboolean old_con_forcedup = con_forcedup;

	scr_con_current = glheight;
	con_forcedup = true;
	Draw_ConsoleBackground (cbx);
	con_forcedup = old_con_forcedup;
	scr_con_current = old_con_current;

	// the startup splash is id1 artwork, mods get the "Loading" pic instead
	if (startup && COM_GetGameNames (false)[0] == 0)
		SCR_DrawStartupSplashPic (cbx);
	else
		SCR_DrawLoadingPic (cbx);
}

/*
==============
SCR_DrawCrosshair -- johnfitz
==============
*/
static void SCR_DrawCrosshair (cb_context_t *cbx)
{
	if (vulkan_globals.stereo_active)
		return;
	if (!crosshair.value || scr_viewsize.value >= 130)
		return;

	GL_SetCanvas (cbx, CANVAS_CROSSHAIR);

	if (crosshair.value)
		M_DrawCrosshair (cbx, 0.0f, 0.0f, CLAMP (6.0f, crosshair_size.value, 64.0f));
}

//=============================================================================

/*
==================
SCR_SetUpToDrawConsole
==================
*/
static void SCR_SetUpToDrawConsole (void)
{
	// johnfitz -- let's hack away the problem of slow console when host_timescale is <0
	extern cvar_t host_timescale;
	float		  timescale;
	// johnfitz

	Con_CheckResize ();

	if (scr_drawloading || (scr_drawstartuploading && cls.state == ca_disconnected))
		return; // never a console with loading plaque

	if (con_forcedup)
	{
		scr_conlines = glheight; // full screen //johnfitz -- glheight instead of vid.height
		scr_con_current = scr_conlines;
	}
	else if (key_dest == key_console)
		scr_conlines = glheight / 2; // half screen //johnfitz -- glheight instead of vid.height
	else
		scr_conlines = 0; // none visible

	timescale = (host_timescale.value > 0) ? host_timescale.value : 1; // johnfitz -- timescale

	if (scr_conanim.value)
	{
		if (scr_conlines < scr_con_current)
		{
			// ericw -- (glheight/600.0) factor makes conspeed resolution independent, using 800x600 as a baseline
			scr_con_current -= scr_conspeed.value * (glheight / 600.0) * host_frametime / timescale; // johnfitz -- timescale
			if (scr_conlines > scr_con_current)
				scr_con_current = scr_conlines;
		}
		else if (scr_conlines > scr_con_current)
		{
			// ericw -- (glheight/600.0)
			scr_con_current += scr_conspeed.value * (glheight / 600.0) * host_frametime / timescale; // johnfitz -- timescale
			if (scr_conlines < scr_con_current)
				scr_con_current = scr_conlines;
		}
	}
	else
	{
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
		else if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;
	}
}

/*
==================
SCR_DrawConsole
==================
*/
static void SCR_DrawConsole (cb_context_t *cbx)
{
	if (scr_con_current)
	{
		Con_DrawConsole (cbx, scr_con_current, true);
		clearconsole = 0;
	}
	else
	{
		if (key_dest == key_game || key_dest == key_message)
			Con_DrawNotify (cbx); // only draw notify in game
	}
}

//=============================================================================

/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true, false);

	if (cls.state == ca_dedicated)
		return;

	// Draw a clean loading frame and freeze screen updates until loading finishes.
	Con_ClearNotify ();
	SCR_CenterPrintClear ();

	// map/connect/load stop the demo loop before getting here, so this only stays set for the initial attract loads
	if (cls.demonum == -1)
		scr_drawstartuploading = false;

	scr_drawloading = true;
	SCR_UpdateScreen (false);
	scr_drawloading = false;
	scr_con_current = 0;

	scr_disabled_for_loading = true;
	scr_disabled_time = realtime;
}

/*
===============
SCR_EndStartupLoadingPlaque

================
*/
void SCR_EndStartupLoadingPlaque (void)
{
	scr_drawstartuploading = false;
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque (void)
{
	scr_disabled_for_loading = false;
	Con_ClearNotify ();
}

//=============================================================================

const char *scr_notifystring;
qboolean	scr_drawdialog;

static void SCR_DrawNotifyString (cb_context_t *cbx)
{
	const char *start;
	int			l;
	int			j;
	int			x, y;

	GL_SetCanvas (cbx, CANVAS_MENU); // johnfitz

	start = scr_notifystring;

	y = 200 * 0.35; // johnfitz -- stretched overlays

	do
	{
		// scan the width of the line
		for (l = 0; start[l]; l++)
			if (start[l] == '\n')
				break;
		x = (320 - l * CHARACTER_SIZE) / 2; // johnfitz -- 320x200 coordinate system
		for (j = 0; j < l; j++, x += CHARACTER_SIZE)
			Draw_Character (cbx, x, y, start[j]);

		y += CHARACTER_SIZE;
		start += l;

		if (!*start)
			break;
		start++; // skip the \n
	} while (1);
}

/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.
==================
*/
static qboolean SCR_ModalInputDone (int key, int character)
{
	return character == 'y' || character == 'Y' || character == 'n' || character == 'N' ||
		key == K_ESCAPE || key == K_ABUTTON || key == K_BBUTTON || key == K_MOUSE2;
}

int SCR_ModalMessage (const char *text, float timeout) // johnfitz -- timeout
{
	double time1, time2; // johnfitz -- timeout
	int	   lastkey, lastchar;

	if (cls.state == ca_dedicated)
		return true;

	scr_notifystring = text;

	// draw a fresh screen
	scr_drawdialog = true;
	SCR_UpdateScreen (false);
	scr_drawdialog = false;

	S_ClearBuffer (); // so dma doesn't loop current sound

	time1 = Sys_DoubleTime () + timeout; // johnfitz -- timeout
	time2 = 0.0f;						 // johnfitz -- timeout

	Key_BeginInputGrab ();
	do
	{
		Sys_SendKeyEvents ();
		Key_GetGrabbedInput (&lastkey, &lastchar);
		if (!SCR_ModalInputDone (lastkey, lastchar) && V_TrackedSessionActive ())
		{
			// The normal host loop is blocked here. Refresh through the same
			// serial XR frame owner so both the dialog and its input stay live.
			scr_drawdialog = true;
			SCR_UpdateScreen (false);
			scr_drawdialog = false;
			VR_InputCommands (GL_OpenXRFrame ());
			Key_GetGrabbedInput (&lastkey, &lastchar);
		}
		Sys_Sleep (16);
		if (timeout)
			time2 = Sys_DoubleTime (); // johnfitz -- zero timeout means wait forever.
	} while (!SCR_ModalInputDone (lastkey, lastchar) && time2 <= time1);
	Key_EndInputGrab ();

	//	SCR_UpdateScreen (); //johnfitz -- commented out

	// johnfitz -- timeout
	if (time2 > time1)
		return false;
	// johnfitz

	return (lastchar == 'y' || lastchar == 'Y' || lastkey == K_ABUTTON);
}

//=============================================================================

// johnfitz -- deleted SCR_BringDownConsole

/*
==================
SCR_TileClear
johnfitz -- modified to use glwidth/glheight instead of vid.width/vid.height
		also fixed the dimentions of right and top panels
==================
*/
void SCR_TileClear (cb_context_t *cbx)
{
	if (r_refdef.vrect.x > 0 || r_refdef.vrect.y > 0)
		GL_SetCanvas (cbx, CANVAS_DEFAULT);

	if (r_refdef.vrect.x > 0)
	{
		// left
		Draw_TileClear (cbx, 0, 0, r_refdef.vrect.x, glheight - sb_lines);
		// right
		Draw_TileClear (cbx, r_refdef.vrect.x + r_refdef.vrect.width, 0, glwidth - r_refdef.vrect.x - r_refdef.vrect.width, glheight - sb_lines);
	}

	if (r_refdef.vrect.y > 0)
	{
		// top
		Draw_TileClear (cbx, r_refdef.vrect.x, 0, r_refdef.vrect.width, r_refdef.vrect.y);
		// bottom
		Draw_TileClear (
			cbx, r_refdef.vrect.x, r_refdef.vrect.y + r_refdef.vrect.height, r_refdef.vrect.width,
			glheight - r_refdef.vrect.y - r_refdef.vrect.height - sb_lines);
	}
}

typedef struct
{
	qboolean valid, pointer_valid;
	int pointer_x, pointer_y;
	float world_per_eye_pixel;
	float world_from_ndc[16];
	float csqc_world_from_ndc[16];
	csqc_display_t csqc_display; // Positive width publishes a fully prepared CSQC transform.
} vr_menu_panel_t;

typedef enum
{
	VR_PANEL_NONE,
	VR_PANEL_MENU,
	VR_PANEL_CONSOLE,
	VR_PANEL_MODAL,
	VR_PANEL_LOADING,
	VR_PANEL_INTERMISSION
} vr_panel_mode_t;

/* SCR_SetupFrame publishes one immutable pose for both eye GUI draws. The
 * anchor is presentation state only; it never changes gameplay aim. */
static vr_menu_panel_t vr_menu_panel;
static vr_menu_anchor_t vr_menu_anchor;
static vr_panel_mode_t vr_menu_panel_mode;
static qboolean vr_modal_from_menu;
static const struct qmodel_s *vr_menu_world;
static int vr_menu_connection;

/* Text popups use one head-relative surface per XR frame. The immutable
 * transform is shared by both eye GUI recordings. */
typedef struct
{
	qboolean valid;
	float world_from_ndc[16];
	qboolean notify_valid;
	float notify_world_from_ndc[16];
} vr_text_popup_panel_t;

static vr_text_popup_panel_t vr_text_popup_panel;

typedef struct
{
	qboolean valid;
	float world_from_ndc[16];
} vr_weapon_menu_panel_t;

static vr_weapon_menu_panel_t vr_weapon_menu_panel;
static vr_menu_anchor_t vr_weapon_menu_anchor;
static unsigned int vr_weapon_menu_anchor_generation;
static int vr_weapon_menu_anchor_mode;
static float vr_weapon_menu_opening_matrix[3][4];
static float vr_weapon_menu_opening_gun_angle;

typedef struct
{
	qboolean valid;
	qboolean csqc_hud;
	csqc_display_t csqc_display;
	float world_from_ndc[16];
} vr_classic_sbar_panel_t;

static vr_classic_sbar_panel_t vr_classic_sbar_panel;

typedef struct
{
	qboolean valid;
	float world_from_ndc[16];
} vr_modern_sbar_panel_t;

static vr_modern_sbar_panel_t vr_modern_sbar_panel;

/* Invert CANVAS_CSQC's full viewport around an explicit source pivot. HUD
 * source y=0 stays at its donor target; intermission centers (160,100). */
static qboolean SCR_VRCSQCPanelPrepare (csqc_display_t *display, float matrix[16],
	float width, float height, float scale, const vec3_t target, const vec3_t right,
	const vec3_t down, const vec3_t normal, float pivot_x, float pivot_y)
{
	display->width = 0.0f;
	if (!isfinite (width) || !isfinite (height) || width <= 0 || height <= 0 ||
		!isfinite (scale) || scale <= 0 || vid.width <= 0 || vid.height <= 0 || glwidth <= 0 || glheight <= 0)
		return false;

	const float viewport_y = vid.height - glheight;
	const float x_step = 2.0f * glwidth / (width * vid.width);
	const float y_step = 2.0f * glheight / (height * vid.height);
	const float x_base = -1.0f;
	const float y_base = 2.0f * viewport_y / vid.height - 1.0f;
	if (!isfinite (x_step) || !isfinite (y_step) || x_step <= 0 || y_step <= 0 || !isfinite (y_base))
		return false;

	const float center_x = x_base + x_step * pivot_x;
	const float center_y = y_base + y_step * pivot_y;
	memset (matrix, 0, 16 * sizeof (*matrix));
	for (int i = 0; i < 3; ++i)
	{
		matrix[i] = right[i] * (scale / x_step);
		matrix[4 + i] = down[i] * (scale / y_step);
		matrix[8 + i] = normal[i] * scale;
		matrix[12 + i] = target[i] - matrix[i] * center_x - matrix[4 + i] * center_y;
		if (!isfinite (matrix[i]) || !isfinite (matrix[4 + i]) || !isfinite (matrix[8 + i]) || !isfinite (matrix[12 + i]))
			return false;
	}
	matrix[15] = 1.0f;
	display->height = height;
	display->scale = 1.0f;
	display->pixel_scale[0] = glwidth / width;
	display->pixel_scale[1] = glheight / height;
	display->width = width;
	return true;
}

static qboolean SCR_VRMenuRayHit (const vec3_t origin, const vec3_t direction,
	const vec3_t center, const vec3_t right, const vec3_t down, const vec3_t normal,
	float scale, qboolean menu_canvas, int *pixel_x, int *pixel_y)
{
	vec3_t relative;
	float ndc_x, ndc_y;
	if (glwidth <= 0 || glheight <= 0)
		return false;
	const float denominator = DotProduct (direction, normal);
	if (!isfinite (denominator) || fabsf (denominator) < 0.0001f)
		return false;
	const float distance = (DotProduct (center, normal) - DotProduct (origin, normal)) / denominator;
	if (!isfinite (distance) || distance <= 0)
		return false;
	for (int i = 0; i < 3; ++i)
		relative[i] = origin[i] + distance * direction[i] - center[i];
	ndc_x = DotProduct (relative, right) / (scale * glwidth * 0.5f);
	ndc_y = DotProduct (relative, down) / (scale * glheight * 0.5f);
	if (!isfinite (ndc_x) || !isfinite (ndc_y) || fabsf (ndc_x) > 1.0f || fabsf (ndc_y) > 1.0f)
		return false;
	*pixel_x = (int)((ndc_x + 1.0f) * 0.5f * glwidth);
	*pixel_y = (int)((ndc_y + 1.0f) * 0.5f * glheight);
	return !menu_canvas || M_VRPointerPixelInMenuCanvas (*pixel_x, *pixel_y);
}

/* Native and CSQC intermission share the existing tracked menu anchor. */
static qboolean SCR_VRNativeIntermission (void)
{
	if (cls.state != ca_connected || cls.signon != SIGNONS || !cl.worldmodel ||
		key_dest != key_game ||
		(cl.intermission != 1 && cl.intermission != 2))
		return false;

	return true;
}

static void SCR_VRMenuPrepare (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	const qboolean frame_available = vulkan_globals.stereo_active && frame && frame->should_render;
	const qboolean loading = scr_drawloading || (scr_drawstartuploading && cls.state == ca_disconnected);
	/* The first selector keeps every tracked panel mode, including this one,
	 * out of the ordinary desktop GUI path. */
	const vr_panel_mode_t requested_mode = !frame_available ? VR_PANEL_NONE :
		scr_drawdialog ? VR_PANEL_MODAL : loading ? VR_PANEL_LOADING :
		SCR_VRNativeIntermission () ? VR_PANEL_INTERMISSION :
		key_dest == key_menu ? (m_state != m_none ? VR_PANEL_MENU : VR_PANEL_NONE) :
		scr_con_current > 0 ? VR_PANEL_CONSOLE : VR_PANEL_NONE;
	vec3_t ray_origin, ray_direction, forward, right, up, down, normal;
	vec3_t head_angles;
	int pointer_x = 0, pointer_y = 0;
	const int follow_mode = isfinite (vr_menu_follow.value) && vr_menu_follow.value >= 0 &&
		vr_menu_follow.value <= 2 ? (int)vr_menu_follow.value : 1;
	const qboolean previous_panel_valid = vr_menu_panel.valid;
	const float previous_scale = vr_menu_panel.world_per_eye_pixel;
	float scale = vr_menu_scale.value;
	float canvas_scale;
	qboolean ray_valid = false, pointing = false;

	vr_menu_panel.valid = false;
	vr_menu_panel.pointer_valid = false;
	vr_menu_panel.csqc_display.width = 0.0f;
	if (requested_mode == VR_PANEL_NONE)
	{
		vr_menu_panel_mode = VR_PANEL_NONE;
		vr_modal_from_menu = false;
		vr_menu_anchor.valid = 0;
		return;
	}
	/* A confirmation opened from the menu belongs on that same physical panel. */
	const qboolean keep_menu_anchor =
		(vr_menu_panel_mode == VR_PANEL_MENU && requested_mode == VR_PANEL_MODAL && key_dest == key_menu) ||
		(vr_menu_panel_mode == VR_PANEL_MODAL && requested_mode == VR_PANEL_MENU && vr_modal_from_menu);
	if (requested_mode == VR_PANEL_MODAL && vr_menu_panel_mode != VR_PANEL_MODAL)
		vr_modal_from_menu = vr_menu_panel_mode == VR_PANEL_MENU && key_dest == key_menu;
	else if (requested_mode != VR_PANEL_MODAL)
		vr_modal_from_menu = false;
	if ((vr_menu_panel_mode != requested_mode && !keep_menu_anchor) || frame->reference_changed ||
		vr_menu_world != cl.worldmodel || vr_menu_connection != cls.state)
		vr_menu_anchor.valid = 0;
	vr_menu_panel_mode = requested_mode;
	if (requested_mode == VR_PANEL_MENU && m_state == m_mods)
		scale *= 1.35f;
	canvas_scale = M_MenuCanvasScale ();
	if (!isfinite (canvas_scale) || canvas_scale <= 0)
	{
		vr_menu_anchor.valid = 0;
		return;
	}
	/* CANVAS_MENU applies scr_menuscale in its ortho. Cancel it so one
	 * source pixel always occupies vr_menu_scale world units on the panel. */
	scale /= canvas_scale;
	if (glwidth <= 0 || glheight <= 0 || !isfinite (scale) || scale <= 0 ||
		!isfinite (scale * glwidth) || !isfinite (scale * glheight) ||
		!frame->devices[0].valid || !frame->devices[0].tracked ||
		frame->devices[0].kind != VRXR_DEVICE_HEAD || frame->devices[0].hand != -1)
	{
		vr_menu_anchor.valid = 0;
		return;
	}
	vr_menu_world = cl.worldmodel;
	vr_menu_connection = cls.state;

	if (requested_mode == VR_PANEL_MENU)
	{
		ray_valid = frame->focused && R_TrackedControllerRay (VR_InputDominantPhysicalHand (), ray_origin, ray_direction);
		if (ray_valid && vr_menu_anchor.valid && previous_panel_valid)
		{
			AngleVectors (vr_menu_anchor.angles, normal, right, up);
			VectorScale (up, -1, down);
			pointing = SCR_VRMenuRayHit (ray_origin, ray_direction, vr_menu_anchor.center,
				right, down, normal, previous_scale, true, &pointer_x, &pointer_y);
		}
	}
	VectorCopy (r_refdef.viewangles, head_angles);
	head_angles[ROLL] = 0;
	if (!VR_MenuAnchorUpdate (&vr_menu_anchor, r_refdef.vieworg, head_angles, Sys_DoubleTime (),
		true, follow_mode, 48.0f, pointing))
		return;

	AngleVectors (vr_menu_anchor.angles, forward, right, up);
	VectorScale (up, -1, down);
	VectorCopy (forward, normal);
	vr_menu_panel.pointer_valid = ray_valid && SCR_VRMenuRayHit (ray_origin, ray_direction,
		vr_menu_anchor.center, right, down, normal, scale, true, &pointer_x, &pointer_y);
	if (vr_menu_panel.pointer_valid)
	{
		vr_menu_panel.pointer_x = pointer_x;
		vr_menu_panel.pointer_y = pointer_y;
	}
	for (int i = 0; i < 16; ++i)
		vr_menu_panel.world_from_ndc[i] = 0;
	for (int i = 0; i < 3; ++i)
	{
		vr_menu_panel.world_from_ndc[i] = right[i] * scale * glwidth * 0.5f;
		vr_menu_panel.world_from_ndc[4 + i] = down[i] * scale * glheight * 0.5f;
		vr_menu_panel.world_from_ndc[8 + i] = normal[i] * scale;
		vr_menu_panel.world_from_ndc[12 + i] = vr_menu_anchor.center[i];
	}
	vr_menu_panel.world_from_ndc[15] = 1;
	vr_menu_panel.world_per_eye_pixel = scale;
	vr_menu_panel.valid = true;
	if (requested_mode == VR_PANEL_INTERMISSION && cl.intermission == 1 &&
		scr_style.value < 1.0f && cl.qcvm.extfuncs.CSQC_DrawScores && !qcvm)
		SCR_VRCSQCPanelPrepare (&vr_menu_panel.csqc_display, vr_menu_panel.csqc_world_from_ndc,
			320.0f, 200.0f, vr_menu_scale.value, vr_menu_anchor.center, right, down, normal, 160.0f, 100.0f);
}

/* Keep popup text at the inherited 48-unit menu distance. At the default
 * world scale this is about 1.83 m, and cancelling the menu canvas scale keeps
 * CANVAS_MENU glyphs at the familiar angular size. */
static void SCR_VRTextPopupPrepare (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	vec3_t head_angles, forward, right, up, down, normal, center;
	float scale, canvas_scale;

	vr_text_popup_panel.valid = false;
	vr_text_popup_panel.notify_valid = false;
	if (!vulkan_globals.stereo_active || !frame || !frame->should_render || !frame->focused ||
		!frame->devices[0].valid || !frame->devices[0].tracked || frame->devices[0].kind != VRXR_DEVICE_HEAD ||
		frame->devices[0].hand != -1 || key_dest != key_game || m_state != m_none ||
		scr_drawdialog || scr_drawloading || scr_con_current > 0.0f || con_forcedup ||
		glwidth <= 0 || glheight <= 0)
		return;
	canvas_scale = M_MenuCanvasScale ();
	if (!isfinite (canvas_scale) || canvas_scale <= 0.0f ||
		!isfinite (vr_menu_scale.value) || vr_menu_scale.value <= 0.0f)
		return;
	scale = vr_menu_scale.value / canvas_scale;
	if (!isfinite (scale) || scale <= 0.0f ||
		!isfinite (scale * glwidth) || !isfinite (scale * glheight))
		return;
	VectorCopy (r_refdef.viewangles, head_angles);
	head_angles[ROLL] = 0.0f;
	AngleVectors (head_angles, forward, right, up);
	VectorScale (up, -1.0f, down);
	VectorCopy (forward, normal);
	VectorMA (r_refdef.vieworg, 48.0f, normal, center);
	for (int i = 0; i < 3; ++i)
		if (!isfinite (center[i]) || !isfinite (right[i]) || !isfinite (down[i]) || !isfinite (normal[i]))
			return;
	memset (vr_text_popup_panel.world_from_ndc, 0, sizeof (vr_text_popup_panel.world_from_ndc));
	for (int i = 0; i < 3; ++i)
	{
		vr_text_popup_panel.world_from_ndc[i] = right[i] * scale * glwidth * 0.5f;
		vr_text_popup_panel.world_from_ndc[4 + i] = down[i] * scale * glheight * 0.5f;
		vr_text_popup_panel.world_from_ndc[8 + i] = normal[i] * scale;
		vr_text_popup_panel.world_from_ndc[12 + i] = center[i];
		if (!isfinite (vr_text_popup_panel.world_from_ndc[i]) ||
			!isfinite (vr_text_popup_panel.world_from_ndc[4 + i]) ||
			!isfinite (vr_text_popup_panel.world_from_ndc[8 + i]) ||
			!isfinite (vr_text_popup_panel.world_from_ndc[12 + i]))
			return;
	}
	vr_text_popup_panel.world_from_ndc[15] = 1.0f;
	vr_text_popup_panel.valid = true;
	/* Notify starts at the console canvas's top edge, not the menu centre.
	 * Bound its physical width independently of eye resolution and place
	 * that top edge just above the forward view. Keep console glyph aspect. */
	if (vid.conwidth > 0 && vid.conheight > 0)
	{
		const float width = fminf (320.0f * vr_menu_scale.value, 64.0f);
		const float height = width * (float)vid.conheight / vid.conwidth;
		if (!isfinite (width) || !isfinite (height) || height <= 0.0f)
			return;
		memset (vr_text_popup_panel.notify_world_from_ndc, 0,
			sizeof (vr_text_popup_panel.notify_world_from_ndc));
		for (int i = 0; i < 3; ++i)
		{
			vr_text_popup_panel.notify_world_from_ndc[i] = right[i] * width * 0.5f;
			vr_text_popup_panel.notify_world_from_ndc[4 + i] = down[i] * height * 0.5f;
			vr_text_popup_panel.notify_world_from_ndc[8 + i] = normal[i];
			vr_text_popup_panel.notify_world_from_ndc[12 + i] =
				center[i] + up[i] * 8.0f + down[i] * height * 0.5f;
		}
		vr_text_popup_panel.notify_world_from_ndc[15] = 1.0f;
		vr_text_popup_panel.notify_valid = true;
	}
}

static void SCR_DrawTextPopupCenterString (cb_context_t *cbx)
{
	if (vr_text_popup_panel.valid)
		GL_BeginUIPanel (cbx, vr_text_popup_panel.world_from_ndc);
	SCR_CheckDrawCenterString (cbx);
	if (vr_text_popup_panel.valid)
		GL_EndUIPanel (cbx);
}

static void SCR_DrawTextPopupConsole (cb_context_t *cbx)
{
	if (vr_text_popup_panel.notify_valid)
		GL_BeginUIPanel (cbx, vr_text_popup_panel.notify_world_from_ndc);
	SCR_DrawConsole (cbx);
	if (vr_text_popup_panel.notify_valid)
		GL_EndUIPanel (cbx);
}

/* Prepare the held weapon wheel once for the stereo pair. Keep the opening
 * grip pose in tracking space and remap it through the current prepared view. */
static void SCR_VRWeaponMenuPrepare (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	vec3_t ray_origin, ray_direction, ray_right, ray_up, hand_origin;
	vec3_t anchor_right, anchor_up, anchor_forward;
	vec3_t forward, right, up, down, normal, view_angles;
	int pointer_x = -1, pointer_y = -1;
	int dominant, ring_count;
	qboolean pointer_valid;
	float scale, min_dimension, live_gun_angle;
	const unsigned int generation = VR_WeaponMenu_SessionGeneration ();
	const int requested_mode = isfinite (vr_weaponmenu_mode.value) &&
		vr_weaponmenu_mode.value >= 0.0f && vr_weaponmenu_mode.value < 2.0f ?
		(int)vr_weaponmenu_mode.value : 0;

	vr_weapon_menu_panel.valid = false;
	if (!VR_WeaponMenu_IsOpenVR ())
	{
		vr_weapon_menu_anchor.valid = 0;
		vr_weapon_menu_anchor_generation = generation;
		return;
	}
	if (generation != vr_weapon_menu_anchor_generation)
	{
		vr_weapon_menu_anchor.valid = 0;
		vr_weapon_menu_anchor_generation = generation;
		vr_weapon_menu_anchor_mode = requested_mode;
	}
	else if (requested_mode != vr_weapon_menu_anchor_mode)
	{
		VR_WeaponMenu_Cancel ();
		vr_weapon_menu_anchor.valid = 0;
		return;
	}
	if (!frame || !frame->should_render || !frame->focused || frame->reference_changed ||
		glwidth <= 0 || glheight <= 0)
	{
		VR_WeaponMenu_Cancel ();
		vr_weapon_menu_anchor.valid = 0;
		return;
	}
	dominant = VR_InputDominantPhysicalHand ();
	if (dominant < 0 || dominant > 1 ||
		!R_TrackedControllerRay (dominant, ray_origin, ray_direction))
	{
		VR_WeaponMenu_Cancel ();
		vr_weapon_menu_anchor.valid = 0;
		return;
	}
	live_gun_angle = V_VRGunAngle ();
	if (!isfinite (live_gun_angle) ||
		!R_TrackedPoseBasis (frame->devices[dominant + 1].matrix, &live_gun_angle,
		ray_origin, ray_right, ray_up, ray_direction))
	{
		VR_WeaponMenu_Cancel ();
		vr_weapon_menu_anchor.valid = 0;
		return;
	}
	ring_count = q_max (1, VR_WeaponMenu_VisibleRingCount ());
	if (vr_weapon_menu_anchor_mode == 0)
	{
		if (!vr_weapon_menu_anchor.valid)
		{
			memcpy (vr_weapon_menu_opening_matrix, frame->devices[dominant + 1].matrix,
				sizeof (vr_weapon_menu_opening_matrix));
			vr_weapon_menu_opening_gun_angle = live_gun_angle;
		}
		if (!R_TrackedPoseBasis (vr_weapon_menu_opening_matrix,
			&vr_weapon_menu_opening_gun_angle, hand_origin,
			anchor_right, anchor_up, anchor_forward))
		{
			VR_WeaponMenu_Cancel ();
			vr_weapon_menu_anchor.valid = 0;
			return;
		}
		/* Re-evaluate the same opening pose against this frame's head, camera,
		 * world scale and prepared stereo basis. */
		vr_weapon_menu_anchor.valid = 1;
		VectorMA (hand_origin,
			10.5f + (ring_count - 1) * 2.5f, anchor_forward,
			vr_weapon_menu_anchor.center);
		VectorCopy (anchor_right, right);
		VectorCopy (anchor_up, up);
		VectorCopy (anchor_forward, forward);
	}
	else
	{
		VectorCopy (r_refdef.viewangles, view_angles);
		if (!VR_MenuAnchorUpdate (&vr_weapon_menu_anchor, r_refdef.vieworg, view_angles,
			Sys_DoubleTime (), true, 2, 48.0f, true))
		{
			VR_WeaponMenu_Cancel ();
			vr_weapon_menu_anchor.valid = 0;
			return;
		}
		/* The helper intentionally clears roll for generic menus. The weapon
		 * wheel keeps the current headset roll in its own prepared frame. */
		vr_weapon_menu_anchor.angles[ROLL] = view_angles[ROLL];
		AngleVectors (vr_weapon_menu_anchor.angles, forward, right, up);
	}
	VectorScale (up, -1.0f, down);
	VectorCopy (forward, normal);
	min_dimension = q_min ((float)glwidth, (float)glheight);
	if (vr_weapon_menu_anchor_mode == 0)
	{
		/* Five Quake units per source ring. Reserve increasing side room for
		 * quick/co-op labels without pushing outer models beyond the canvas. */
		const float outer_radius = 5.0f * q_max (1, ring_count - 1);
		const float margin = 3.0f + (ring_count - 1) * 1.5f;
		scale = (outer_radius + margin) / (0.45f * min_dimension);
	}
	else
	{
		scale = vr_menu_scale.value;
		if (!isfinite (scale) || scale <= 0.0f)
			scale = 0.13f;
		/* The wheel uses the full-resolution default canvas. Convert the menu's
		 * 320-pixel physical scale to that canvas so headset resolution does not
		 * make the panel grow. */
		scale *= 320.0f / min_dimension;
	}
	if (!isfinite (scale) || scale <= 0.0f)
	{
		VR_WeaponMenu_Cancel ();
		vr_weapon_menu_anchor.valid = 0;
		return;
	}
	pointer_valid = SCR_VRMenuRayHit (ray_origin, ray_direction,
		vr_weapon_menu_anchor.center, right, down, normal, scale, false,
		&pointer_x, &pointer_y);
	if (pointer_valid && vr_weapon_menu_anchor_mode == 0 && cl.worldmodel &&
		!cl.worldmodel->needload)
	{
		vec3_t target, impact, hit_normal, remaining;
		for (int axis = 0; axis < 3; ++axis)
			target[axis] = vr_weapon_menu_anchor.center[axis] +
				(pointer_x - glwidth * 0.5f) * scale * right[axis] +
				(pointer_y - glheight * 0.5f) * scale * down[axis];
		CL_TraceWorldLine (ray_origin, target, impact, hit_normal);
		VectorSubtract (target, impact, remaining);
		if (VectorLength (remaining) >= 1.0f)
			pointer_valid = false;
	}
	memset (vr_weapon_menu_panel.world_from_ndc, 0, sizeof (vr_weapon_menu_panel.world_from_ndc));
	for (int i = 0; i < 3; ++i)
	{
		float *matrix = vr_weapon_menu_panel.world_from_ndc;
		matrix[i] = right[i] * (scale * glwidth * 0.5f);
		matrix[4 + i] = down[i] * (scale * glheight * 0.5f);
		matrix[8 + i] = normal[i] * scale;
		matrix[12 + i] = vr_weapon_menu_anchor.center[i];
		if (!isfinite (matrix[i]) || !isfinite (matrix[4 + i]) || !isfinite (matrix[8 + i]) ||
			!isfinite (matrix[12 + i]))
		{
			VR_WeaponMenu_Cancel ();
			vr_weapon_menu_anchor.valid = 0;
			return;
		}
	}
	vr_weapon_menu_panel.world_from_ndc[15] = 1.0f;
	VR_WeaponMenu_SetVRPointer (true, pointer_valid, pointer_x, pointer_y,
		vr_weapon_menu_panel.world_from_ndc, ray_origin, ray_direction,
		vr_weapon_menu_anchor_mode == 0);
	if (!VR_WeaponMenu_IsOpenVR ())
	{
		vr_weapon_menu_anchor.valid = 0;
		return;
	}
	vr_weapon_menu_panel.valid = true;
	VR_WeaponMenu_SetVRPanel (vr_weapon_menu_panel.world_from_ndc,
		vr_weapon_menu_anchor_mode == 0);
}

/* One donor placement rule for classic, CSQC, and modern presentation. */
static qboolean SCR_VRHUDPose (vec3_t target, vec3_t right, vec3_t down, vec3_t normal)
{
	vec3_t aim_angles, panel_angles, forward, up, hand_origin, hand_direction;
	int dominant;

	if (vr_aimmode.value == VR_AIMMODE_CONTROLLER)
	{
		dominant = VR_InputDominantPhysicalHand ();
		if (!R_TrackedControllerRay (dominant, hand_origin, hand_direction) ||
			!V_TrackedMovementAngles (VR_MOVEMENT_MODE_FOLLOW_HAND, dominant, aim_angles))
			return false;
		/* The hand roll affects the donor's side offset, but not panel tilt. */
		AngleVectors (aim_angles, forward, right, up);
		VectorMA (hand_origin, dominant == 0 ? 5.0f : -5.0f, right, target);
		aim_angles[ROLL] = 0;
	}
	else
	{
		VectorCopy (cl.viewangles, aim_angles);
		if (vr_aimmode.value == VR_AIMMODE_HEAD_MYAW ||
			vr_aimmode.value == VR_AIMMODE_HEAD_MYAW_MPITCH)
			aim_angles[PITCH] = 0;
		aim_angles[ROLL] = 0;
		AngleVectors (aim_angles, forward, right, up);
		VectorMA (cl.viewent.origin, 1.0f, forward, target);
		const float *offset = V_GetPredictionViewOffset ();
		if (!offset)
			return false;
		for (int i = 0; i < 3; ++i)
		{
			if (!isfinite (offset[i]))
				return false;
			target[i] += offset[i];
			if (!isfinite (target[i]))
				return false;
		}
	}

	VectorCopy (aim_angles, panel_angles);
	panel_angles[PITCH] += 45.0f;
	panel_angles[ROLL] = 0;
	AngleVectors (panel_angles, normal, right, up);
	VectorScale (up, -1.0f, down);
	VectorMA (target, 10.0f, normal, target);
	for (int i = 0; i < 3; ++i)
		if (!isfinite (target[i]) || !isfinite (right[i]) || !isfinite (down[i]) || !isfinite (normal[i]))
			return false;
	return true;
}

static void SCR_VRFieldPanelPrepare (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	vec3_t target, right, down, normal;

	vr_field_panel_pose.valid = false;
	if (!r_showfields.value || VEC_SIZE (bbox_linked) == 0 ||
		!SCR_VRHUDFrameEligible (frame) || scr_drawdialog || scr_drawloading ||
		(vr_menu_panel_mode == VR_PANEL_MENU && vr_menu_panel.valid) ||
		vid.width <= 0 || vid.height <= 0 || glwidth <= 0 || glheight <= 0)
		return;
	if (!SCR_VRHUDPose (target, right, down, normal))
		return;
	VectorCopy (target, vr_field_panel_pose.target);
	VectorCopy (right, vr_field_panel_pose.right);
	VectorCopy (down, vr_field_panel_pose.down);
	VectorCopy (normal, vr_field_panel_pose.normal);
	vr_field_panel_pose.valid = true;
}

/* Map the classic 320x48 CANVAS_SBAR coordinates through the same viewport
 * and ortho math as GL_SetCanvas. Inverting that affine keeps each canvas
 * unit at vr_hud_scale world units regardless of render or bar scale. */
static void SCR_VRClassicSbarPrepare (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	vec3_t target, right, normal, down;
	float scale, bar_scale, viewport_x, viewport_y, x_step, y_step, x_base, y_base;
	float canvas_width = 320.0f;
	qboolean csqc_hud;

	vr_classic_sbar_panel.valid = false;
	if (!SCR_VRClassicSbarFrameEligible (frame))
		return;
	csqc_hud = scr_style.value < 1.0f && !qcvm &&
		(cl.qcvm.extfuncs.CSQC_DrawHud ||
		 (cl.qcvm.extfuncs.CSQC_DrawScores && !cl.qcvm.extfuncs.CSQC_DrawHud &&
		  (sb_showscores || cl.stats[STAT_HEALTH] <= 0) && key_dest != key_menu));
	if (csqc_hud)
	{
		const csqc_display_t display = SCR_GetCSQCDisplay ();
		if (isfinite (display.width) && isfinite (display.scale) && display.width > 0.0f && display.scale > 0.0f)
			canvas_width = q_max (320.0f, display.width / display.scale);
	}

	scale = vr_hud_scale.value;
	if (!isfinite (scale) || scale <= 0 || vid.width <= 0 || vid.height <= 0 || glwidth <= 0 || glheight <= 0)
		return;

	if (!SCR_VRHUDPose (target, right, down, normal))
		return;

	if (csqc_hud)
	{
		vr_classic_sbar_panel.valid = SCR_VRCSQCPanelPrepare (&vr_classic_sbar_panel.csqc_display,
			vr_classic_sbar_panel.world_from_ndc, canvas_width, 200.0f, scale,
			target, right, down, normal, canvas_width * 0.5f, 0.0f);
		vr_classic_sbar_panel.csqc_hud = csqc_hud;
		return;
	}
	bar_scale = CLAMP (1.0f, scr_sbarscale.value, (float)glwidth / 320.0f);
	if (!isfinite (bar_scale) || bar_scale <= 0)
		return;
	viewport_x = (glwidth - 320.0f * bar_scale) * 0.5f;
	viewport_y = vid.height - 48.0f * bar_scale;
	x_step = 2.0f * bar_scale / vid.width;
	y_step = 2.0f * bar_scale / vid.height;
	x_base = 2.0f * viewport_x / vid.width - 1.0f;
	y_base = 2.0f * viewport_y / vid.height - 1.0f;
	if (!isfinite (x_step) || !isfinite (y_step) || x_step <= 0 || y_step <= 0 ||
		!isfinite (x_base) || !isfinite (y_base))
		return;

	memset (vr_classic_sbar_panel.world_from_ndc, 0, sizeof (vr_classic_sbar_panel.world_from_ndc));
	for (int i = 0; i < 3; ++i)
	{
		float *matrix = vr_classic_sbar_panel.world_from_ndc;
		matrix[i] = right[i] * (scale / x_step);
		matrix[4 + i] = down[i] * (scale / y_step);
		matrix[8 + i] = normal[i] * scale;
		const float center_x = x_base + x_step * 160.0f;
		matrix[12 + i] = target[i] - matrix[i] * center_x - matrix[4 + i] * y_base;
		if (!isfinite (matrix[i]) || !isfinite (matrix[4 + i]) || !isfinite (matrix[8 + i]) ||
			!isfinite (matrix[12 + i]))
			return;
	}
	vr_classic_sbar_panel.world_from_ndc[15] = 1.0f;
	vr_classic_sbar_panel.csqc_hud = csqc_hud;
	vr_classic_sbar_panel.valid = true;
}

/* Place the modern 640x400 surface on the same frozen donor pose as the
 * classic HUD. The viewport fit is inverted here, so each source unit keeps
 * the configured physical size at every framebuffer resolution. */
static void SCR_VRModernSbarPrepare (void)
{
	const vrxr_frame_t *frame = GL_OpenXRFrame ();
	vec3_t target, right, normal, down;
	float scale, fitting_scale, origin_x, origin_y, ndc_center_x, ndc_top_y;

	vr_modern_sbar_panel.valid = false;
	if (!SCR_VRModernSbarFrameEligible (frame) || vid.width <= 0 || vid.height <= 0 || glwidth <= 0 || glheight <= 0)
		return;

	scale = vr_hud_scale.value;
	if (!SCR_VRHUDPose (target, right, down, normal))
		return;

	fitting_scale = q_min ((float)glwidth / 640.0f, (float)glheight / 400.0f);
	if (!isfinite (fitting_scale) || fitting_scale <= 0.0f)
		return;
	origin_x = (glwidth - 640.0f * fitting_scale) * 0.5f;
	origin_y = (glheight - 400.0f * fitting_scale) * 0.5f;
	ndc_center_x = 2.0f * (origin_x + 320.0f * fitting_scale) / vid.width - 1.0f;
	ndc_top_y = 2.0f * (vid.height - glheight + origin_y) / vid.height - 1.0f;
	if (!isfinite (ndc_center_x) || !isfinite (ndc_top_y))
		return;

	memset (vr_modern_sbar_panel.world_from_ndc, 0, sizeof (vr_modern_sbar_panel.world_from_ndc));
	for (int i = 0; i < 3; ++i)
	{
		float *matrix = vr_modern_sbar_panel.world_from_ndc;
		matrix[i] = right[i] * (scale * vid.width / (2.0f * fitting_scale));
		matrix[4 + i] = down[i] * (scale * vid.height / (2.0f * fitting_scale));
		matrix[8 + i] = normal[i] * scale;
		matrix[12 + i] = target[i] - matrix[i] * ndc_center_x - matrix[4 + i] * ndc_top_y;
		if (!isfinite (matrix[i]) || !isfinite (matrix[4 + i]) || !isfinite (matrix[8 + i]) ||
			!isfinite (matrix[12 + i]))
			return;
	}
	vr_modern_sbar_panel.world_from_ndc[15] = 1.0f;
	vr_modern_sbar_panel.valid = true;
}

/* Draw the live status bar through its prepared VR transform when available. */
static void SCR_DrawVRHUDPanel (cb_context_t *cbx, qboolean flat_fallback)
{
	const qboolean modern_panel = vulkan_globals.stereo_active && vr_modern_sbar_panel.valid;
	const qboolean classic_panel = vulkan_globals.stereo_active && vr_classic_sbar_panel.valid;
	const qboolean draw_panel = modern_panel || classic_panel;
	const qboolean panel_csqc = classic_panel && vr_classic_sbar_panel.csqc_hud;

	if (!draw_panel)
	{
		if (flat_fallback)
			Sbar_Draw (cbx);
		return;
	}

	if (modern_panel)
	{
		GL_BeginUIPanel (cbx, vr_modern_sbar_panel.world_from_ndc);
		cbx->ui_panel_modern_hud = true;
	}
	else
	{
		GL_BeginUIPanel (cbx, vr_classic_sbar_panel.world_from_ndc);
		cbx->ui_panel_classic_hud = true;
	}
	if (panel_csqc)
		SCR_SetCSQCDisplayOverride (&vr_classic_sbar_panel.csqc_display);
	Sbar_Draw (cbx);
	SCR_SetCSQCDisplayOverride (NULL);
	GL_EndUIPanel (cbx);
}

/*
==================
SCR_DrawGUI
==================
*/
static void SCR_DrawGUI (void *unused)
{
	cb_context_t *cbx = vulkan_globals.secondary_cb_contexts[SCBX_GUI];
	const qboolean console_panel_valid = vulkan_globals.stereo_active &&
		vr_menu_panel_mode == VR_PANEL_CONSOLE && vr_menu_panel.valid;
	const qboolean menu_panel_valid = vulkan_globals.stereo_active &&
		vr_menu_panel_mode == VR_PANEL_MENU && vr_menu_panel.valid;
	const qboolean modal_panel_valid = vulkan_globals.stereo_active &&
		vr_menu_panel_mode == VR_PANEL_MODAL && vr_menu_panel.valid;
	const qboolean loading_panel_valid = vulkan_globals.stereo_active &&
		vr_menu_panel_mode == VR_PANEL_LOADING && vr_menu_panel.valid;
	const qboolean intermission_panel_valid = vulkan_globals.stereo_active &&
		vr_menu_panel_mode == VR_PANEL_INTERMISSION && vr_menu_panel.valid;
	volatile qboolean recovered_csqc_error = false;
	scr_csqc_error_phase = SCR_CSQC_ERROR_IDLE;
	scr_draw_gui_owns_qc_mutex = false;
	GL_DrawSceneUpscale (cbx);
	if (vulkan_globals.stereo_active && key_dest == key_menu)
		M_SetVRPointerPixelPosition (vr_menu_panel.pointer_x, vr_menu_panel.pointer_y,
			vr_menu_panel_mode == VR_PANEL_MENU && vr_menu_panel.valid && vr_menu_panel.pointer_valid);
	GL_SetCanvas (cbx, CANVAS_DEFAULT);
	R_BindGraphicsPipeline (cbx, PIPELINE_BASIC_BLEND);

	// FIXME: only call this when needed
	R_BeginDebugUtilsLabel (cbx, "2D");
	if (!menu_panel_valid && !intermission_panel_valid)
		SCR_TileClear (cbx);

	const int csqc_items_before = cl.stats[STAT_ITEMS];

	if (setjmp (screen_error))
	{
		/* Host_Error jumps out of CSQC_DrawHud without unwinding this draw.
		 * Restore the UI command state and release the GUI lock before clearing
		 * the failing QCVM, or the next draw will deadlock on the same mutex. */
		scr_csqc_error_phase = SCR_CSQC_ERROR_CLEANUP;
		SCR_SetCSQCDisplayOverride (NULL);
		if (cbx->ui_panel_active)
			GL_EndUIPanel (cbx);
		/* Clearing the failed QCVM changes Sbar_Draw back to classic; its
		 * canvas must not reuse the CSQC-sized panel transform below. */
		vr_classic_sbar_panel.valid = false;
		cl.stats[STAT_ITEMS] = csqc_items_before;
		if (scr_draw_gui_owns_qc_mutex)
		{
			scr_draw_gui_owns_qc_mutex = false;
			SDL_UnlockMutex (draw_qcvm_mutex);
		}
		PR_ClearProgs (&cl.qcvm);
		recovered_csqc_error = true;
	}

	SDL_LockMutex (draw_qcvm_mutex);
	scr_draw_gui_owns_qc_mutex = true;
	if (!recovered_csqc_error)
		scr_csqc_error_phase = SCR_CSQC_ERROR_ARMED;

	if (menu_panel_valid)
	{
		/* The menu and HUD keep separate physical anchors. On a recovered
		 * CSQC failure the menu is already recorded, so redraw only the HUD. */
		if (!recovered_csqc_error)
		{
			GL_BeginUIPanel (cbx, vr_menu_panel.world_from_ndc);
			M_Draw (cbx);
			if (vr_menu_panel.pointer_valid)
			{
				const float scale = M_MenuCanvasScale ();
				/* Match M_PixelToMenuCanvasCoord so source units scale with menu glyphs. */
				const float pointer_x = (vr_menu_panel.pointer_x - (glwidth - 320.0f * scale) * 0.5f) / scale;
				const float pointer_y = (vr_menu_panel.pointer_y - (glheight - 200.0f * scale) * 0.5f) / scale;

				GL_SetCanvas (cbx, CANVAS_MENU);
				Draw_Fill (cbx, pointer_x - 4, pointer_y - 1, 9, 3, 15, 1.0f);
				Draw_Fill (cbx, pointer_x - 1, pointer_y - 4, 3, 9, 15, 1.0f);
			}
			GL_EndUIPanel (cbx);
		}
		SCR_DrawVRHUDPanel (cbx, !con_forcedup);
	}
	else if (scr_drawdialog) // new game confirm
	{
		SCR_DrawVRHUDPanel (cbx, !con_forcedup);
		if (modal_panel_valid)
		{
			GL_BeginUIPanel (cbx, vr_menu_panel.world_from_ndc);
			if (con_forcedup)
				Draw_ConsoleBackground (cbx);
			Draw_FadeScreen (cbx);
			SCR_DrawNotifyString (cbx);
			GL_EndUIPanel (cbx);
		}
		else
		{
			if (con_forcedup)
				Draw_ConsoleBackground (cbx);
			Draw_FadeScreen (cbx);
			SCR_DrawNotifyString (cbx);
		}
	}
	else if (scr_drawloading || (scr_drawstartuploading && cls.state == ca_disconnected)) // loading
	{
		if (!recovered_csqc_error && loading_panel_valid)
		{
			GL_BeginUIPanel (cbx, vr_menu_panel.world_from_ndc);
			SCR_DrawMenuLoading (cbx, scr_drawstartuploading);
			GL_EndUIPanel (cbx);
		}
		else if (!recovered_csqc_error)
			SCR_DrawMenuLoading (cbx, scr_drawstartuploading);
		if (vulkan_globals.stereo_active && !con_forcedup)
			SCR_DrawVRHUDPanel (cbx, false);
	}
	else if (cl.intermission == 1 && key_dest == key_game) // end of level
	{
		const qboolean panel_csqc = intermission_panel_valid &&
			vr_menu_panel.csqc_display.width > 0.0f && !recovered_csqc_error;
		if (intermission_panel_valid)
		{
			GL_BeginUIPanel (cbx, panel_csqc ? vr_menu_panel.csqc_world_from_ndc : vr_menu_panel.world_from_ndc);
			if (panel_csqc)
				SCR_SetCSQCDisplayOverride (&vr_menu_panel.csqc_display);
		}
		Sbar_IntermissionOverlay (cbx);
		if (intermission_panel_valid)
		{
			SCR_SetCSQCDisplayOverride (NULL);
			GL_EndUIPanel (cbx);
		}
	}
	else if (cl.intermission == 2 && key_dest == key_game) // end of episode
	{
		if (intermission_panel_valid)
			GL_BeginUIPanel (cbx, vr_menu_panel.world_from_ndc);
		Sbar_FinaleOverlay (cbx);
		if (vr_text_popup_panel.valid)
		{
			if (intermission_panel_valid)
				GL_EndUIPanel (cbx);
			SCR_DrawTextPopupCenterString (cbx);
		}
		else
		{
			SCR_CheckDrawCenterString (cbx);
			if (intermission_panel_valid)
				GL_EndUIPanel (cbx);
		}
	}
	else
	{
		SCR_DrawCrosshair (cbx); // johnfitz
		SCR_DrawNet (cbx);
		SCR_DrawTurtle (cbx);
		SCR_DrawPause (cbx);
		SCR_DrawTextPopupCenterString (cbx);
		SCR_DrawVRHUDPanel (cbx, true);
		SCR_DrawDevStats (cbx); // johnfitz
		SCR_DrawFPS (cbx);		// johnfitz
		SCR_DrawSpeeds (cbx);
		SCR_DrawClock (cbx); // johnfitz
		SCR_DrawEdictInfo (cbx);
		if (!console_panel_valid)
			SCR_DrawTextPopupConsole (cbx);
		M_Draw (cbx);
		if (console_panel_valid)
		{
			GL_BeginUIPanel (cbx, vr_menu_panel.world_from_ndc);
			SCR_DrawConsole (cbx);
			GL_EndUIPanel (cbx);
		}
		if (VR_WeaponMenu_IsOpenVR ())
		{
			if (vr_weapon_menu_panel.valid)
			{
				GL_BeginUIPanel (cbx, vr_weapon_menu_panel.world_from_ndc);
				VR_WeaponMenu_Draw (cbx);
				GL_EndUIPanel (cbx);
			}
		}
		else
			VR_WeaponMenu_Draw (cbx);
	}

	scr_csqc_error_phase = SCR_CSQC_ERROR_IDLE;
	scr_draw_gui_owns_qc_mutex = false;
	SDL_UnlockMutex (draw_qcvm_mutex);
	R_EndDebugUtilsLabel (cbx);
}

/*
==================
SCR_SetupFrame
==================
*/
typedef struct
{
	qboolean weapon_menu_pointer_valid;
	int weapon_menu_pointer_x, weapon_menu_pointer_y;
} scr_setup_frame_t;

static void SCR_SetupFrame (void *unused)
{
	const scr_setup_frame_t *setup = (const scr_setup_frame_t *)unused;
	vr_field_panel_pose.valid = false;
	if (!vulkan_globals.stereo_active)
	{
		SCR_SetUpToDrawConsole ();
		V_SetupFrame ();
	}
	R_PrepareStereoFrame ();
	VR_WeaponMenu_SetDesktopPointer (setup && setup->weapon_menu_pointer_valid,
		setup ? setup->weapon_menu_pointer_x : -1,
		setup ? setup->weapon_menu_pointer_y : -1);
	// Entity-light fading needs the current frame's actual eye origins in VR.
	// Keep this before surface marking and all consumers of cl_dlights.
	if (!con_forcedup)
		R_UpdateEntityDlights ();
	SCR_VRMenuPrepare ();
	SCR_VRTextPopupPrepare ();
	SCR_VRWeaponMenuPrepare ();
	SCR_VRClassicSbarPrepare ();
	SCR_VRModernSbarPrepare ();
	SCR_VRFieldPanelPrepare ();
}

/*
==================
SCR_DrawDone
==================
*/
static void SCR_DrawDone (void *unused)
{
	if (scr_speeds.value)
		rs_cputime_us = (uint32_t)((Sys_DoubleTime () - rs_frame_starttime) * 1000000.0);
	// end_rendering depends on draw_done, so this can't lose a wait from the current frame
	rs_gpuwaittime_us = rs_gpuwaitaccum_us;
	rs_gpuwaitaccum_us = 0;
	r_framecount++;
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
/* Host aborts can leave a serial refresh between XR begin and submission.
 * The recoverable CSQC HUD longjmp stays inside SCR_DrawGUI and does not use
 * this path. Join submitted donor work before runtime release or camera reset. */
void SCR_AbortXRFrame (void)
{
	if (!vulkan_globals.stereo_active)
		return;
	// Begin rotates donor dynamic-buffer and garbage rings before submission.
	// An abandoned begin must retire the preceding GPU users before another
	// begin rotates again without advancing the corresponding command slot.
	GL_WaitForDeviceIdle ();
	/* The wait also joins the end-render task before changing shared view state. */
	V_ClearWeaponCollisionPresentation ();
	VRXR_AbortFrame ();
	R_RestoreStereoView ();
	in_update_screen = false;
}

void SCR_UpdateScreen (qboolean use_tasks)
{
	scr_setup_frame_t setup_frame = {0};
	if (!scr_initialized || !con_initialized || in_update_screen)
		return; // not initialized yet

	if (Tasks_IsWorker ())
		return; // not safe

	in_update_screen = true;
	use_tasks = use_tasks && (Tasks_NumWorkers () > 1) && r_tasks.value && r_gpulightmapupdate.value;

	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > 60)
		{
			scr_disabled_for_loading = false;
			Con_Printf ("load failed.\n");
		}
		else
		{
			GL_InvalidateXRInput ();
			in_update_screen = false;
			return;
		}
	}

	if (vid.recalc_refdef)
		SCR_CalcRefdef ();

	// decide on the height of the console
	con_forcedup = !cl.worldmodel || cls.signon != SIGNONS;

	task_handle_t begin_rendering_task = INVALID_TASK_HANDLE;
	if (!GL_BeginRendering (use_tasks, &begin_rendering_task, &glwidth, &glheight))
	{
		V_ClearWeaponCollisionPresentation ();
		V_ClearAkimboPair ();
		in_update_screen = false;
		return;
	}
	if (VR_WeaponMenu_IsOpen () && !VR_WeaponMenu_IsOpenVR () &&
		vid.width > 0 && vid.height > 0 && glwidth > 0 && glheight > 0)
	{
		int mouse_x, mouse_y;
		IN_GetMousePos (&mouse_x, &mouse_y);
		setup_frame.weapon_menu_pointer_valid = true;
		setup_frame.weapon_menu_pointer_x = (int)((double)mouse_x * glwidth / vid.width);
		setup_frame.weapon_menu_pointer_y = (int)((double)mouse_y * glheight / vid.height);
	}
	/* Observe the equipped viewmodel before preparing wheel assets or starting
	 * draw tasks. Runtime discoveries share the existing catalog. */
	VR_WeaponMenu_ObserveActive ();
	VR_WeaponMenu_PrepareModels ();
	/* XR synchronizes the previous GUI task in GL_BeginRendering. Sample the
	 * current console animation before deciding whether its HUD reserves rows;
	 * keep desktop console updates in the original setup task. */
	if (vulkan_globals.stereo_active)
		SCR_SetUpToDrawConsole ();
	// Publish gameplay aim once on the main owner, before view/draw tasks.
	V_UpdateTrackedAim ();
	if (vid.recalc_refdef || SCR_VRClassicSbarFrameEligible (GL_OpenXRFrame ()) != scr_vr_classic_sbar_refdef_active)
		SCR_CalcRefdef ();
	if (vulkan_globals.stereo_active)
	{
		/* CL_TraceWeapon borrows main-thread PMove hull scratch. Prepare the
		 * tracked viewmodel and its shared eye/crosshair pose before tasks. */
		V_SetupFrame ();
		V_PrepareAkimboPair ();
		V_PrepareWeaponCollisionPresentation ();
	}
	else
		V_ClearAkimboPair ();
	R_PrepareVRCrosshair ();
	/* Admit optional avatar meshes before render tasks can inspect model or
	 * texture tables. A first load may join the previous end task; do this
	 * before that task is transferred to the new begin dependency below. */
	R_VRIKRenderStageAvatars ();

	if (use_tasks)
	{
		if (prev_end_rendering_task != INVALID_TASK_HANDLE)
		{
			Task_AddDependency (prev_end_rendering_task, begin_rendering_task);
			prev_end_rendering_task = INVALID_TASK_HANDLE;
		}

		task_handle_t draw_done_task = Task_AllocateAndAssignFunc (SCR_DrawDone, NULL, 0);
		task_handle_t setup_frame_task = Task_AllocateAndAssignFunc (SCR_SetupFrame,
			&setup_frame, sizeof (setup_frame));
		task_handle_t draw_gui_task = Task_AllocateAndAssignFunc (SCR_DrawGUI, NULL, 0);
		V_RenderView (use_tasks, begin_rendering_task, setup_frame_task, draw_done_task, draw_gui_task);
		task_handle_t end_rendering_task = GL_EndRendering (use_tasks, true);

		if (vulkan_globals.stereo_active)
			Task_AddDependency (begin_rendering_task, setup_frame_task);
		Task_AddDependency (begin_rendering_task, draw_gui_task);
		Task_AddDependency (setup_frame_task, draw_gui_task);
		Task_AddDependency (draw_gui_task, draw_done_task);
		Task_AddDependency (draw_done_task, end_rendering_task);

		task_handle_t tasks[] = {begin_rendering_task, setup_frame_task, draw_done_task, draw_gui_task, end_rendering_task};
		Tasks_Submit (sizeof (tasks) / sizeof (task_handle_t), tasks);

		while (!Task_Join (draw_done_task, 10))
			S_ExtraUpdate ();
		prev_end_rendering_task = end_rendering_task;
	}
	else
	{
		GL_SynchronizeEndRenderingTask ();
		SCR_SetupFrame (&setup_frame);
		V_RenderView (use_tasks, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE, INVALID_TASK_HANDLE);
		S_ExtraUpdate ();
		SCR_DrawGUI (NULL);
		SCR_DrawDone (NULL);
		GL_EndRendering (false, true);
	}

	GL_EndXRFrame ();
	R_RestoreStereoView ();
	in_update_screen = false;
	/* Key_Event may open a nested modal refresh. Dispatch only after the
	 * finished menu draw has published hover and the XR frame is released. */
	if (vulkan_globals.stereo_active && key_dest == key_menu && m_state != m_none && !scr_drawdialog &&
		!scr_drawloading && !(scr_drawstartuploading && cls.state == ca_disconnected))
		VR_InputMenuPanelTrigger (GL_OpenXRFrame (), vr_menu_panel.valid);
}
