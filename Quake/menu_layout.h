/* Shared geometry for the mods browser's menu-specific canvas. */
#ifndef _QUAKE_MENU_LAYOUT_H
#define _QUAKE_MENU_LAYOUT_H

#define MOD_BROWSER_ROWS 24
#define MOD_BROWSER_CANVAS_HEIGHT 264

#define GRAPHICS_MENU_ROWS 15

typedef struct graphics_menu_layout_s
{
	int category_y;
	int list_top;
	int row_height;
	int rows;
	int help_y;
	int detail_help_y;
	int list_right;
	int slider_x;
	int slider_size;
} graphics_menu_layout_t;

static inline graphics_menu_layout_t GraphicsMenu_Layout (void)
{
	const graphics_menu_layout_t layout = {
		32, 48, 8, GRAPHICS_MENU_ROWS, 176, 184, 312, 216, 6,
	};

	return layout;
}

static inline int GraphicsMenu_VisibleRows (int item_count, int first_row)
{
	const graphics_menu_layout_t layout = GraphicsMenu_Layout ();
	const int remaining = item_count - first_row;

	return remaining <= 0 ? 0 : (remaining < layout.rows ? remaining : layout.rows);
}

static inline int GraphicsMenu_RowY (int visible_row)
{
	const graphics_menu_layout_t layout = GraphicsMenu_Layout ();

	return layout.list_top + visible_row * layout.row_height;
}

static inline int GraphicsMenu_RowIndex (int first_row, int visible_row)
{
	return first_row + visible_row;
}

static inline int GraphicsMenu_NextAASample (int current, int direction, unsigned supported_samples)
{
	static const int choices[] = {0, 2, 4, 8, 16};
	int index = 0;

	for (int i = 1; i < (int)(sizeof (choices) / sizeof (choices[0])); ++i)
		if (current >= choices[i])
			index = i;
	for (int i = 0; i < (int)(sizeof (choices) / sizeof (choices[0])); ++i)
	{
		index = (index + (int)(sizeof (choices) / sizeof (choices[0])) + direction) % (int)(sizeof (choices) / sizeof (choices[0]));
		if (!choices[index] || (supported_samples & (unsigned)choices[index]))
			return choices[index];
	}

	return current;
}

typedef struct mod_browser_layout_s
{
	int list_top;
	int row_height;
	int rows;
	int list_right;
	int controls_y;
	int search_box_y;
	int search_text_y;
} mod_browser_layout_t;

static inline mod_browser_layout_t ModBrowser_Layout (void)
{
	const mod_browser_layout_t layout = {
		32, 8, MOD_BROWSER_ROWS, 312,
		224, 232, 240,
	};

	return layout;
}

/* Pointer targets in the mod browser. A desktop mouse and the VR laser both
 * arrive here in menu canvas coordinates, so draw-time hover feedback,
 * hover registration and click handling share one geometry. */
enum
{
	MOD_BROWSER_CONTROL_NONE = -1,
	MOD_BROWSER_CONTROL_TAB = 0,	 /* Installed <-> Downloads */
	MOD_BROWSER_CONTROL_REFRESH = 1, /* Scan installed / refresh catalogue */
	MOD_BROWSER_CONTROL_SEARCH = 2,	 /* Filter box, opens the on-screen keyboard */
	MOD_BROWSER_CONTROL_CONFIRM = 2, /* Details: confirm install */
	MOD_BROWSER_CONTROL_BACK = 3,	 /* Details: back, or cancel while installing */
};

#define MOD_BROWSER_DETAIL_BUTTON_Y		 182
#define MOD_BROWSER_DETAIL_BUTTON_HEIGHT 10
#define MOD_BROWSER_DETAIL_SPLIT_X		 200

/* Inclusive left/right/top/bottom, matching M_Mouse_InRect. */
typedef struct mod_browser_control_rect_s
{
	int left, right, top, bottom;
} mod_browser_control_rect_t;

static inline int ModBrowser_InControlRect (mod_browser_control_rect_t r, int x, int y)
{
	return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

static inline mod_browser_control_rect_t ModBrowser_ControlRect (int control)
{
	const mod_browser_layout_t layout = ModBrowser_Layout ();
	mod_browser_control_rect_t r = {0, -1, 0, -1};

	switch (control)
	{
	case MOD_BROWSER_CONTROL_TAB:
		r.left = 12, r.right = 175, r.top = layout.controls_y, r.bottom = layout.controls_y + layout.row_height - 1;
		break;
	case MOD_BROWSER_CONTROL_REFRESH:
		r.left = 176, r.right = layout.list_right, r.top = layout.controls_y, r.bottom = layout.controls_y + layout.row_height - 1;
		break;
	case MOD_BROWSER_CONTROL_SEARCH:
		r.left = 12, r.right = layout.list_right, r.top = layout.search_box_y, r.bottom = layout.search_box_y + 23;
		break;
	}
	return r;
}

/* Controls below the list: tab, refresh and the filter box. */
static inline int ModBrowser_ControlAt (int x, int y)
{
	for (int control = MOD_BROWSER_CONTROL_TAB; control <= MOD_BROWSER_CONTROL_SEARCH; ++control)
		if (ModBrowser_InControlRect (ModBrowser_ControlRect (control), x, y))
			return control;
	return MOD_BROWSER_CONTROL_NONE;
}

static inline mod_browser_control_rect_t ModBrowser_DetailButtonRect (int control, int installing)
{
	mod_browser_control_rect_t r = {0, -1, MOD_BROWSER_DETAIL_BUTTON_Y, MOD_BROWSER_DETAIL_BUTTON_Y + MOD_BROWSER_DETAIL_BUTTON_HEIGHT};
	const mod_browser_layout_t layout = ModBrowser_Layout ();

	if (installing)
	{
		/* Only "Cancel and return" exists while a download runs. */
		if (control == MOD_BROWSER_CONTROL_BACK)
			r.left = 12, r.right = layout.list_right;
	}
	else if (control == MOD_BROWSER_CONTROL_CONFIRM)
		r.left = 12, r.right = MOD_BROWSER_DETAIL_SPLIT_X - 1;
	else if (control == MOD_BROWSER_CONTROL_BACK)
		r.left = MOD_BROWSER_DETAIL_SPLIT_X, r.right = layout.list_right;
	return r;
}

/* Catalogue details / download progress buttons. */
static inline int ModBrowser_DetailButtonAt (int x, int y, int installing)
{
	if (ModBrowser_InControlRect (ModBrowser_DetailButtonRect (MOD_BROWSER_CONTROL_BACK, installing), x, y))
		return MOD_BROWSER_CONTROL_BACK;
	if (ModBrowser_InControlRect (ModBrowser_DetailButtonRect (MOD_BROWSER_CONTROL_CONFIRM, installing), x, y))
		return MOD_BROWSER_CONTROL_CONFIRM;
	return MOD_BROWSER_CONTROL_NONE;
}

/* x for the list scrollbar. The shared menus put it at preferred_x, outside
 * the 320-wide canvas; on a narrow or square display (VR eye buffers) that
 * can fall off the visible panel, leaving the laser nothing to drag. Pull it
 * back inside the visible area then, but never over the list's left side. */
static inline int ModBrowser_ScrollbarX (int preferred_x, int bar_width, float visible_right)
{
	const mod_browser_layout_t layout = ModBrowser_Layout ();

	if (visible_right >= (float)(preferred_x + bar_width))
		return preferred_x;
	/* Comparisons are false for NaN, which falls back to the list edge. */
	if (visible_right > (float)(layout.list_right + bar_width))
		return (int)visible_right - bar_width;
	return layout.list_right;
}

/* Visible list row under the pointer for a click, or -1. The bottom edge is
 * exclusive so the controls row directly below never selects a mod. */
static inline int ModBrowser_RowAt (int x, int y, int visible_rows)
{
	const mod_browser_layout_t layout = ModBrowser_Layout ();

	if (visible_rows <= 0 || x < 12 || x > layout.list_right || y < layout.list_top ||
		y >= layout.list_top + visible_rows * layout.row_height)
		return -1;
	return (y - layout.list_top) / layout.row_height;
}

#endif /* _QUAKE_MENU_LAYOUT_H */
