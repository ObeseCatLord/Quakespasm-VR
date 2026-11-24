#include <assert.h>
#include <stdio.h>

#include "../Quake/menu_layout.h"

/* Pointer (desktop mouse or VR laser) to mod browser target mapping. */
static void test_mod_browser_pointer_targets (void)
{
	const mod_browser_layout_t layout = ModBrowser_Layout ();
	const int bottom_row_y = layout.list_top + (layout.rows - 1) * layout.row_height;

	/* List rows: every visible row maps to itself, nothing past the last. */
	for (int row = 0; row < layout.rows; ++row)
	{
		const int y = layout.list_top + row * layout.row_height;
		assert (ModBrowser_RowAt (12, y, layout.rows) == row);
		assert (ModBrowser_RowAt (layout.list_right, y + layout.row_height - 1, layout.rows) == row);
	}
	assert (ModBrowser_RowAt (11, layout.list_top, layout.rows) == -1);
	assert (ModBrowser_RowAt (layout.list_right + 1, layout.list_top, layout.rows) == -1);
	assert (ModBrowser_RowAt (100, layout.list_top - 1, layout.rows) == -1);
	assert (ModBrowser_RowAt (100, layout.list_top + 3 * layout.row_height, 3) == -1);
	assert (ModBrowser_RowAt (100, layout.list_top + 2 * layout.row_height, 3) == 2);
	assert (ModBrowser_RowAt (100, layout.list_top, 0) == -1);
	/* The last row never bleeds into the controls row below it. */
	assert (ModBrowser_RowAt (100, bottom_row_y + layout.row_height, layout.rows) == -1);
	assert (ModBrowser_ControlAt (100, bottom_row_y + layout.row_height - 1) == MOD_BROWSER_CONTROL_NONE);

	/* Controls: tab | refresh split at x=176, filter box below. */
	assert (ModBrowser_ControlAt (12, layout.controls_y) == MOD_BROWSER_CONTROL_TAB);
	assert (ModBrowser_ControlAt (175, layout.controls_y + 7) == MOD_BROWSER_CONTROL_TAB);
	assert (ModBrowser_ControlAt (176, layout.controls_y) == MOD_BROWSER_CONTROL_REFRESH);
	assert (ModBrowser_ControlAt (312, layout.controls_y + 7) == MOD_BROWSER_CONTROL_REFRESH);
	assert (ModBrowser_ControlAt (313, layout.controls_y) == MOD_BROWSER_CONTROL_NONE);
	assert (ModBrowser_ControlAt (11, layout.controls_y) == MOD_BROWSER_CONTROL_NONE);
	assert (ModBrowser_ControlAt (100, layout.search_box_y) == MOD_BROWSER_CONTROL_SEARCH);
	assert (ModBrowser_ControlAt (100, layout.search_box_y + 23) == MOD_BROWSER_CONTROL_SEARCH);
	assert (ModBrowser_ControlAt (100, layout.search_box_y + 24) == MOD_BROWSER_CONTROL_NONE);
	/* An invalid VR ray (-1,-1) and a disabled desktop mouse hit nothing. */
	assert (ModBrowser_ControlAt (-1, -1) == MOD_BROWSER_CONTROL_NONE);
	assert (ModBrowser_DetailButtonAt (-1, -1, 0) == MOD_BROWSER_CONTROL_NONE);
	assert (ModBrowser_RowAt (-1, -1, layout.rows) == -1);

	/* Details dialog: Confirm install | Back. */
	assert (ModBrowser_DetailButtonAt (12, MOD_BROWSER_DETAIL_BUTTON_Y, 0) == MOD_BROWSER_CONTROL_CONFIRM);
	assert (ModBrowser_DetailButtonAt (199, MOD_BROWSER_DETAIL_BUTTON_Y + 10, 0) == MOD_BROWSER_CONTROL_CONFIRM);
	assert (ModBrowser_DetailButtonAt (200, MOD_BROWSER_DETAIL_BUTTON_Y, 0) == MOD_BROWSER_CONTROL_BACK);
	assert (ModBrowser_DetailButtonAt (312, MOD_BROWSER_DETAIL_BUTTON_Y + 10, 0) == MOD_BROWSER_CONTROL_BACK);
	assert (ModBrowser_DetailButtonAt (100, MOD_BROWSER_DETAIL_BUTTON_Y - 1, 0) == MOD_BROWSER_CONTROL_NONE);
	assert (ModBrowser_DetailButtonAt (100, MOD_BROWSER_DETAIL_BUTTON_Y + 11, 0) == MOD_BROWSER_CONTROL_NONE);
	/* While installing the whole strip is "Cancel and return"; confirm is gone. */
	assert (ModBrowser_DetailButtonAt (12, MOD_BROWSER_DETAIL_BUTTON_Y, 1) == MOD_BROWSER_CONTROL_BACK);
	assert (ModBrowser_DetailButtonAt (312, MOD_BROWSER_DETAIL_BUTTON_Y + 10, 1) == MOD_BROWSER_CONTROL_BACK);
	assert (ModBrowser_DetailButtonAt (313, MOD_BROWSER_DETAIL_BUTTON_Y, 1) == MOD_BROWSER_CONTROL_NONE);

	/* Scrollbar: keeps the shared x=376 when the display shows it, otherwise
	 * moves inside the visible panel, never left of the list's right edge. */
	assert (ModBrowser_ScrollbarX (376, 8, 500.0f) == 376);
	assert (ModBrowser_ScrollbarX (376, 8, 384.0f) == 376);
	assert (ModBrowser_ScrollbarX (376, 8, 383.9f) == 375);
	assert (ModBrowser_ScrollbarX (376, 8, 340.0f) == 332);
	assert (ModBrowser_ScrollbarX (376, 8, 320.0f) == layout.list_right);
	assert (ModBrowser_ScrollbarX (376, 8, 0.0f / 0.0f) == layout.list_right);
	for (float right = 320.0f; right < 400.0f; right += 0.5f)
	{
		const int x = ModBrowser_ScrollbarX (376, 8, right);
		assert (x + 8 <= right + 0.001f || x == layout.list_right);
		assert (x >= layout.list_right);
	}

	/* Every target lies inside the 320-wide, MOD_BROWSER_CANVAS_HEIGHT-high
	 * panel backing, so a laser can always reach it. */
	for (int control = MOD_BROWSER_CONTROL_TAB; control <= MOD_BROWSER_CONTROL_SEARCH; ++control)
	{
		const mod_browser_control_rect_t r = ModBrowser_ControlRect (control);
		assert (r.left >= 0 && r.right < 320 && r.top >= 0 && r.bottom < MOD_BROWSER_CANVAS_HEIGHT && r.left <= r.right);
	}
	for (int installing = 0; installing < 2; ++installing)
	{
		const mod_browser_control_rect_t back = ModBrowser_DetailButtonRect (MOD_BROWSER_CONTROL_BACK, installing);
		assert (back.left >= 0 && back.right < 320 && back.left <= back.right);
	}
}

int main (void)
{
	const mod_browser_layout_t layout = ModBrowser_Layout ();

	assert (layout.rows == MOD_BROWSER_ROWS);
	assert (layout.list_top + layout.rows * layout.row_height == layout.controls_y);
	assert (layout.controls_y + layout.row_height == layout.search_box_y);
	assert (layout.search_text_y == layout.search_box_y + layout.row_height);
	assert (layout.search_box_y + 24 <= MOD_BROWSER_CANVAS_HEIGHT);
	assert (layout.list_right <= 320);
	assert (GraphicsMenu_Layout ().list_top == 48);
	test_mod_browser_pointer_targets ();
	puts ("menu UI layout fixture passed");
	return 0;
}
