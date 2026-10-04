#include <assert.h>
#include <stdio.h>

#include "../Quake/menu_layout.h"

int main (void)
{
	const graphics_menu_layout_t layout = GraphicsMenu_Layout ();

	assert (layout.category_y == 32);
	assert (layout.list_top == 48);
	assert (layout.rows == 15);
	assert (GraphicsMenu_RowY (0) == 48);
	assert (GraphicsMenu_RowY (layout.rows - 1) == 160);
	assert (GraphicsMenu_RowY (layout.rows - 1) + layout.row_height - 1 == 167);
	assert (layout.help_y == 176);
	assert (layout.detail_help_y == 184);
	assert (layout.slider_x == 216);
	assert (layout.slider_size == 6);
	assert (GraphicsMenu_VisibleRows (20, 0) == layout.rows);
	assert (GraphicsMenu_VisibleRows (20, 5) == layout.rows);
	assert (GraphicsMenu_VisibleRows (20, 10) == 10);
	assert (GraphicsMenu_RowIndex (5, 9) == 14);
	assert (GraphicsMenu_NextAASample (0, 1, 2 | 4 | 8) == 2);
	assert (GraphicsMenu_NextAASample (4, 1, 2 | 4 | 8) == 8);
	assert (GraphicsMenu_NextAASample (8, 1, 2 | 4 | 8) == 0);
	assert (GraphicsMenu_NextAASample (0, -1, 2 | 4 | 8) == 8);
	assert (GraphicsMenu_NextAASample (8, 1, 2 | 4 | 8 | 16) == 16);
	puts ("graphics menu layout fixture passed");
	return 0;
}
