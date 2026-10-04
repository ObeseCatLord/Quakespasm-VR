# Mods menu follow-up: minimal adapter

Scope: branch `2.0`, `Quake/menu.c`, this document, and the subsequently
authorized Ironwail `Misc/vq_pak/gfx/menumods.lmp` engine resource and its pak
contents entry. No backend, XR/input,
renderer, audio, server, legacy-source, deployment, or platform-build changes.
Other agents own adjacent work; preserve concurrent changes. Runtime validation
is explicitly held for integration.

## Verified presentation reference (before implementation)

- Ironwail `Quake/menu.c:1150-1206` defines Mods immediately after Options and
  inserts its art at source split 60: original top, Mods, original bottom shifted
  by 20 pixels. It uses `gfx/menumods.lmp` or colored 16-pixel text.
- Current `Quake/menu.c:848-939` instead appends 8-pixel Mods without menu2 and
  changes action indices when menu2 exists. Draw, hover, and activation must use
  a single six-row order: Single Player, Multiplayer, Options, Mods, Help, Quit.
- The bundled `Misc/vq_pak/gfx/mainmenu2.lmp` is 240x123. Visual inspection of its
  PNG source confirms Options / Help / Mods / Quit. It is suitable if source
  rows 60-79 (Help) and 80-99 (Mods) are exchanged at draw time; no derived game
  assets are needed. Keep custom mod mainmenu art by slicing its existing image
  at 60 and inserting menumods art or the text fallback. The user subsequently
  authorized bundling Ironwail's actual engine resource to make that art available
  for mods instead of relying on the console-font fallback.
- Existing `Draw_SubPic` (`Quake/gl_draw.c:869-955`) takes normalized source
  offsets plus extents, not endpoint coordinates. `Draw_String_Scaled`
  (`Quake/gl_draw.c:766-797`, declared in `draw.h`) already supplies the fallback.
  No draw-layer helper or generated assets are required.

## Behavioral reference and reusable owners

`AddonCatalog` remains the sole catalogue/worker/install owner. Keep the
snapshot lists, bounded search/filter, on-screen keyboard, detail screen,
approved-entry revalidation (`menu.c:5609-5637`), installation completion,
cancel, and installed-game activation. Only presentation and input adaptation
may change. `M_Mods_SetCatalogue`, `M_Mods_RefreshCatalogue`, and existing detail
actions are the boundaries; no parallel downloader or new asynchronous states.

`Quake/menu_layout.h` already gives 24 rows at y=32..223, controls at y=224,
search box at y=232..255, and a 264-pixel menu canvas. Use the final eight pixels
for a concise mode-specific hint and keep all list geometry intact.

## VR routing audit / final adapter decision

Read-only Sol high audit confirms `vr_input.c:3883-3935`: secondary/menu emits
Escape/B (back), dominant primary emits X (refresh), offhand stick click or Vive
pad click emits LTHUMB (existing catalogue toggle), and offhand menu axes emit
arrows. Index dominant stick click is ALTFIRE, so a generic L3/R3 hint is false.
Index pad click emits Y; Y is not a universal VR search control. The post-render
trigger owner (`vr_input.c:4393-4403`) emits Mouse1 on a valid pointer hit, Enter
otherwise; the search keyboard deliberately requires a hit.

Choose the smallest adapter: in tracked VR's browser list only, offhand Right
opens Downloads and Left returns to Installed. Reuse the existing toggle action
for offhand click and pointer selection, and refresh the existing owner on first
entry from IDLE. Keep primary refresh and secondary back/cancel. Make tabs visible
as filled controls, keep search clickable through its full labelled region, and
display VR-specific select/back/refresh/search/approval hints. Touch/Index have
primary buttons but Vive/simple do not (`vr_openxr.cpp:709-716`), so displayed
refresh/scan hints name pointer-trigger activation; retain primary's existing
shortcut without assuming it on every profile. No additional
focus state or navigation state machine is needed. Ordinary keyboard/gamepad
left/right still scrolls long labels. Detail and on-screen keyboard handlers
retain their existing action routing.

OpenXR has no separate `xr_input` source here: `vr_openxr.cpp:1006` fills the
hand frame, `host.c:1239` forwards it to shared `VR_InputCommands`, and
`vr_input.c:3975` dispatches native key events. Pointer panel pixels reach
`M_SetVRPointerPixelPosition` via `gl_screen.c` (read-only). The existing
`M_PixelToMenuCanvasCoord` (`menu.c:352`) removes letterbox offsets and menu
scale using the menu's 264-pixel canvas; `M_UpdateMouse` copies those transformed
coordinates into `m_mouse_x/y`. New control handlers and hover registration
consume those same coordinates; do not apply another transformation.

Preserved detail behavior: trigger over Confirm/Back clicks that action; outside
a clickable target, shared VR routing falls back to Enter and explicitly
confirms the selected package. The menu continues to revalidate the approved
snapshot before starting installation. Do not mistake a valid ray over detail
text for a pointer-only confirmation gate.

## Architecture comparison and proof

A menu-only adapter reuses draw functions and existing catalogue actions. A
rewrite or additional downloader would duplicate request state, search,
approval, and installation policy with no demonstrated backend incompatibility;
reject it. Expected complexity: one small main-art slicing helper, one stable
row enum, and bounded control/hint changes within Mods menu handlers. Reopen
the design if additional service/input state becomes necessary.

Static verification: diff/scope review, existing menu-layout fixture, syntax
checks against existing native compile flags without producing engine objects.
No runtime or platform build now. At integration, the smallest end-to-end proof
is main-menu Mods by pointer and stick; installed list -> downloader -> refresh
-> entry details -> explicit approval -> install -> installed activation, plus
back/cancel and on-screen search. Check 24 visible rows and transformed hover
targets in desktop and tracked VR, with and without menu2/custom menu art.

## Implementation / static verification

Implemented in `menu.c` only: stable six-row enum and art slicing; shared native
control action; first-IDLE-entry catalogue refresh; VR offhand left/right tab
adapter; filled control strip; expanded labelled search hit region; mode-specific
hints in browser, keyboard, and approval/progress screens. The 24-row layout,
canvas height, backend, input owners, and draw functions remain reusable.

### Engine resource provenance and credit

`Misc/vq_pak/gfx/menumods.lmp` is an unchanged copy of Ironwail's tracked
`Misc/pak/gfx/menumods.lmp` (84x20, 1,688 bytes), credited to Andrei Drexler.
SHA-256: `ca8ed1a5ab5cc97e044a4b41f68e0bb9499635bc44659d710d58158830b53872`.
It was introduced in Ironwail commit
`7da8471d6650c6613d7d6b4b15e38b0a8ea90a9b` (2022-04-30, "Add basic 'Mods' menu")
and last changed in
`ad98904c564f8d1d2b645c1458d38fc3264ae5ca` (2022-08-29, "Tweak Mods item image").
Ironwail distributes this engine resource with its root GPL version 2 license,
`ironwail/LICENSE.txt`; this workspace retains that GPL text in
[LICENSE.txt](../LICENSE.txt). No separate asset license/exception was found.
This is an engine-repository resource copy, not glyphs extracted from an id1
game archive. Preserve this attribution and the GPL notice when redistributing.
The pak contents entry includes it in the existing generated/embedded engine
pak at integration; the pak was not regenerated during this task.

Passed native `-fsyntax-only -Wall -Werror` checks using the existing build graph's
flags with and without `USE_CURL`, without PCH or engine object output. The old
no-curl build graph refers to relocated source paths, so its direct invocation
could not find the source; the no-curl syntax check uses the current native
graph with curl disabled. Existing `tests/menu_ui_layout_fixture.c` passes as a
standalone fixture and verifies the 24 rows, control/search placement, and canvas
bounds. `git diff --check` passes. Runtime is held for integration; no platform
build, commit, push, generated game asset, or deployment was performed. The
authorized engine-resource copy was checked byte-for-byte against its donor;
its qpic header/payload dimensions and pak-contents entry were also checked.

Remaining integration checks: ray hits at control boundaries and scaling;
Vive/Touch/Index/simple profiles (simple uses pointer controls without axes);
custom five-row menu art and high-resolution replacements; cancelled catalogue
refresh then retry; install approval/cancel/completion and search on a real
catalogue. No demonstrated backend edit is required.
