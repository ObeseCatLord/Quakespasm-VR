# Inherited mod-filter keyboard on the native browser

2026-09-30. Remaining UI-002 input behavior, separate from explicit mod launch.
Read-only primary pin51b452c0 supplies a 43-key search keyboard at menu.c:
2994–3073,3134–3160,3296–3360 and pointer hit testing at4471–4480. Its
mod_browser_layout.h defines the shared key rectangles and nearest-center
vertical navigation. Copy those key helpers and activation semantics; retain
vkQuake's installed/catalogue list, metadata, ticker, filtering, installation
approval and map/skill launch owners. Do not replace the native browser layout
with the primary browser merely to recover controller text input.

## Verified native boundary and minimal adapter

Native menu.c:4330–4353 owns one mods_search buffer and installed/catalogue
view. M_Mods_UpdateFilter (:4406) is the sole filter update. M_Mods_Char (:4908)
already bounds printable input at32 characters; M_Mods_Key (:4771) supports
backspace, delete and native list/catalogue actions. The visible Filter field
at:4747 has no mouse/VR activation rectangle, and no on-screen keyboard is
present. Actual end-to-end VR input is unverified.

Use an ordinary boolean subpage and one cursor within m_mods; no new menu state,
text-entry service or filter copy. Open it by selecting the labelled Filter
field using native mouse/VR hover and by an explicit controller shortcut. Keep
existing Tab/L3/R3 installed/catalogue switching and X/F1 refresh semantics.
Show an appropriate shortcut hint; use a free controller key such as Y only
after checking native keys for collisions. Opening/closing calls the existing
menu-change/pointer-reset owner so a previous list hover cannot activate a key.

Reuse the primary alphanumeric, space, punctuation, Backspace, Clear and Done
keys and its shared rectangles/vertical movement. Extract only the reusable
key-count/columns/rectangle/containment/vertical helpers into a small attributed
header; no unused primary browser-control array or second browser scanner.
Adapt drawing to cb_context and existing M_Print/Draw_Fill helpers; register
each rectangle with the native mouse/VR hover owner and use the same geometry
for activation. Keep labels within the logical320x200 menu canvas.

Characters and edits mutate mods_search through its native bounded char/filter
owners. Backspace/delete/clear update the same filter, reset list cursor/scroll
through existing policy and leave the keyboard open. Done/Escape/B closes only
the keyboard and preserves its filter; later Back follows ordinary Mods behavior.
Physical typing still uses M_Mods_Char. Never emit a game/console command for
search text, download automatically or alter approved catalogue entries.

Native M_Draw resets hover each draw and the common M_Mouse_ClickValid gate
controls VR pointer clicks (:7190,7339). Reuse it; no separate XR key mapping or
GUI lock. Catalogue-details/install controls remain authoritative and should
not be overlaid by keyboard entry. Clear subpage state when entering Mods or
changing view/closing menus so hidden keyboard state cannot intercept input.

## Scope and acceptance

Write set: Quake/menu.c Mods region, one pure pointer-policy query beside
existing native pointer APIs, its declaration in Quake/menu.h, the corresponding
single fallback-dispatch condition in Quake/vr_input.c, and
Quake/mod_browser_keyboard.h.
Estimate <=230 added lines. Reopen if a second input/browser owner, renderer
change or broader browser replacement becomes necessary. Main reviews source
against the primary and native pointer/filter boundaries. Plan precedes code
on2.0; user-owned migration-2.0.md and main remain untouched.

No builds/tests/probes now. After all implementation, final Linux/ARM checks
cover controller navigation, native mouse/VR hover and activation, physical
typing, bounded full buffers, space/punctuation/backspace/clear/done, nearest
column movement through the final three wide keys, installed/catalogue filters,
details/install cancellation, pointer hover reset, long metadata and launch
after filtering. User live headset/performance checks remain deferred. This
slice alone does not certify UI-002 or the migration goal.

## Source implementation checkpoint

Main extracted the primary's 43-key geometry and nearest-center vertical helper
into mod_browser_keyboard.h, keeping its original behavior and attribution.
The native Mods page now opens that keyboard from its labelled Filter field
or Y, an unused ordinary controller shortcut in this page. Existing OpenXR
Index PAD maps to Y; other profiles can activate Filter through the same native
pointer/trigger path without another XR mapping. Installed/catalogue controls
and their metadata/installation approval remain native.

The subpage uses only mods_search, M_Mods_Char and M_Mods_UpdateFilter for text
and list updates. Draw and hover share the extracted rectangles, with native
inclusive hover bounds narrowed to match the half-open key rectangles. Native
menu-change handling retires old hover, and keyboard transitions release
slider/scrollbar grabs. Back closes only the keyboard; Done retains the filter.
Entering Mods, changing catalogue view and launching a game clear the subpage.

Main inspected actual pointer/trigger routing and bounded edits against the
primary source; scoped whitespace checks passed. This is source integration,
not executable keyboard or full UI-002 acceptance. Consolidated Linux/ARM
qualification remains pending. No builds, tests or engine probes have run.

## Requested-Astra source disposition before boundary correction

The first bounded read-only source advisory found three P2 defects. Main
verified the named native VR fallback, right-stick keys and scrollbar drag
dispatch. Adopt all three: add the inherited right-stick up/down cases, refuse
Filter mouse activation during scrollbar capture, and suppress a VR trigger's
fallback Enter only while this keyboard requires a pointer hit. Deliberate
physical/controller Enter/A still activate the selected key. The native pointer
dispatcher and other menus retain their current fallback behavior.

The narrow pointer query/condition is a demonstrated incompatibility at the
existing boundary, not another input service. The write set above is amended
before implementing that correction; the <=230-line estimate remains adequate.
Effective model settings are unexposed, so this is a requested-Astra advisory,
not a certified senior-skill pass. No executable checks have run.

The final bounded source recheck confirmed all three findings resolved, with
no new P1/P2 finding in the slice. A missed VR trigger press latches the existing
menu_trigger_key to -1, which both native dispatch paths ignore; release
hysteresis clears it. Holding a missed press while moving onto a key cannot
activate that key later. No additional input-state flag was introduced.
