#include "ui_panel_push_constants.inc"

#if defined(UI_PANEL)
layout (location = 4) in vec2 in_ui_position;

#define UI_PANEL_DISCARD()                                                                                                  \
	if ((push_constants.ui_panel_enabled != 0.0f) &&                                                                          \
		((in_ui_position.x < push_constants.ui_clip_rect.x) || (in_ui_position.y < push_constants.ui_clip_rect.y) ||           \
		 (in_ui_position.x > push_constants.ui_clip_rect.z) || (in_ui_position.y > push_constants.ui_clip_rect.w)))             \
		discard
#else
#define UI_PANEL_DISCARD()
#endif
