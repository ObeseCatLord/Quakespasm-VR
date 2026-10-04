# CAND-UX-003: live visual previews and mods browser

The graphics menu keeps its existing cvar ownership. When a previewable visual
setting changes during an active game, `ui_live_preview` briefly reduces the
existing menu veil so the already-applied renderer state is visible. The same
fade value is used by the desktop veil and the VR menu panel; it does not move
the camera, add a desktop overlay to VR, or alter gameplay. Set
`ui_live_preview 0` to opt out, or use the Graphics menu's Live Preview row.

The mods browser has twenty-four readable 8-pixel rows in a menu-specific
320x264 canvas. The normal 320x200 canvas remains unchanged.
`Quake/menu_layout.h` is the single source for the list, footer, search field,
scrollbar, and pointer-hit geometry. Catalogue/install approval, reconnect,
keyboard filtering, and the virtual keyboard retain their existing owners.

The Graphics Lighting category exposes renderer-owned `r_clustered_lights` as
Dynamic Lighting Mode and `r_surface_dither` as Surface Dither. Dynamic
Lighting Mode shows the requested cvar value; its selected-row help names the
Native fallback whenever shadows, GPU lightmap updates, or dynamic lights make
clustered lighting ineligible. It leaves those settings unchanged.
Surface Dither selects Off, Low, Medium, or High (0, 0.5, 1, or 2). Both rows
use the existing graphics cursor, mouse, VR bindings, and live-preview path;
the renderer remains the only cvar-state owner.

Graphics now uses a 320x200 categorized viewport: Display, Lighting, Effects,
and Advanced, plus a nested Particle Details category. Its shared layout has a
category row at y=32, fifteen rows from y=48 through y=167, and help at y=176
and y=184. Requested values remain visible when renderer capability or map
state selects a native effective fallback. AA choices come from the renderer's
sample-mask query; Video remains the owner of display resolution and mode.
The current stable option mapping is deliberately bounded to fifteen entries
per category, matching the native 320x200 viewport. A future category with
more entries must widen that mapping and retain the shared row/hit/scroll
layout rather than silently overflowing it.
