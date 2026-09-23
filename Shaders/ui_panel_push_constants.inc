#if defined(UI_PANEL)
#define UI_PANEL_PUSH_CONSTANT_MEMBERS \
	vec4 ui_clip_rect;                 \
	float ui_panel_enabled;
#else
#define UI_PANEL_PUSH_CONSTANT_MEMBERS
#endif
