#include <assert.h>
#include <stdio.h>

#include "../Quake/menu_layout.h"

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
	puts ("menu UI layout fixture passed");
	return 0;
}
