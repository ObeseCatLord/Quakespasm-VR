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

#endif /* _QUAKE_MENU_LAYOUT_H */
