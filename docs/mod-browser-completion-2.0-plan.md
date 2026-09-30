# Installed browser refresh, VR paging and bounded metadata

2026-09-30. UI-002 source closure following the launch/filter-keyboard adapters.
Keep the native vkQuake menu, metadata scanner, scrollbar/ticker and copied add-on
catalogue owners. Main/master and dirty docs/migration-2.0.md are untouched.
No builds/tests/probes until all implementation is done.

## Behavioral reference and confirmed source gaps

Primary51b452c0 menu.c:3077 rebuilds installed mods when the menu opens, and
:3473–3483 provides explicit Scan/Refresh through F1/controller X. Its
host_cmd.c:543–547 Modlist_Rebuild is simply FileList_Clear plus Modlist_Init.
Primary menu.c:3410–3422 maps secondary VR-stick events to page navigation.
Native M_Menu_Mods_f (:4904–4919) only copies the startup list. Native refresh
at5259 is catalogue-only, and its generic scrollbar keys (:596–670) have page
keys but no K_VR_RIGHT_STICK_UP/DOWN. Opening the native menu therefore misses
externally added mods; no equivalent Scan action or secondary-stick paging was
found. These are behavior gaps, not a request to replace the native layout.

Native installed metadata already uses descript.ion, mapdb.json, official add-on
names and localization through host_cmd.c:597–713. Native scrolling names and
catalogue installed/verified state remain. Catalogue confirmation compares the
approved name/gamedir/author/description/download/size/verified record with the
current entry before installing (:4802–4831); preserve that full-data validation.

Native M_Mods_PrintWrapped (:4877) wraps all text at36 columns without a row
limit. Confirmation draws name at40 before gamedir label56, gamedir at64 before
author label72, author at80 before description label96, and description at104
before package size144. Long valid metadata can consequently overlap the next
field and install/cancel/status controls. Bound those visual regions while
preserving the complete approved record used for confirmation.

## Minimal adapter and ownership

Production write set: Quake/menu.c, host_cmd.c and quakedef.h; <=100 net lines.
Port the four-line Modlist_Rebuild wrapper into the existing filesystem-list
owner with one prototype. Reuse its native roots, allocation, metadata and
platform behavior; do not copy another root scanner or retain stale entries.

One menu-local refresh helper clears borrowed mods_sorted/mods_filtered pointers
before the native list rebuild, reconstructs them and reapplies the existing
filter. Retire native hover/drag geometry through M_MenuChanged and existing
grab variables. Preserve the selected directory using a bounded temporary copy
before freeing list nodes, then restore it if still present; otherwise use native
clamping. No persistent selection registry or string ownership layer. Opening
Mods invokes this helper after its normal page/filter reset. Explicit installed
Scan uses the same helper, preserving the filter. Copy no native baseline input
changes outside this page. Add X/F1 and a drawn/hit-tested Scan control in the
existing row144 (right-hand refresh area); catalogue refresh remains in that
area. Prevent scans while the catalogue operation is busy, consistent with the
primary. Completed catalogue installs may use the same refresh helper where
appropriate; keep their existing selected-game/status behavior.

After the filter keyboard's early dispatch, map secondary VR-stick up/down to
the existing native PageUp/PageDown handler for list navigation. Keep keyboard
vertical navigation, ordinary desktop ticker/arrows, primary-stick row selection
and trigger selection intact. No general input/backend rewrite or new axis state.

For confirmation metadata, reuse native text drawing with explicit visual row
budgets: name2 rows (40/48), gamedir1 (64), author2 (80/88), description4
(104..128). Ellipsize the final available row or use the existing horizontal
scrolling line for a one-row field. Keep all draw/hit controls at144 and below
unobscured. Do not truncate the stored approved/current catalogue record or weaken
its confirmation comparison. No new text layout service, renderer or page.

## Verified lifetime and final software checks

Main source reads show host.c:1239 SCR_UpdateScreen joins draw_done, which depends
on draw_gui (gl_screen.c:2571–2587), before the following frame's key/command
dispatch. The list rebuild executes in that existing between-frame menu boundary.
Console completion stores the modlist head address; installed-server-game lookups
are synchronous current-list consumers. No addon worker accesses modlist. Do not
retain a freed list node/name across rebuild, and review these boundaries again
if implementation introduces another consumer.

After full implementation, Linux/ARM checks cover add/remove/rename mods during
an open engine, entry/explicit scan, friendly/rerelease/long names, retained filter/
selection, empty results, primary/secondary stick paging, desktop navigation,
pointer hover/scan/scrollbar isolation, catalogue busy/cancel/install completion,
and long metadata with stable confirmation controls. Source/diff review before
commit; software/rendered UI/controller checks stay deferred. This bounded plan
does not certify all add-on install paths or broader migration completion.

## Source implementation checkpoint

The delegated adapter adds58 net lines in the three planned files. Main verified
the native node layout and temporary selection copy, clearing both borrowed
vectors before native list freeing, ordered rebuild/filter/selection restoration,
hover/drag retirement, secondary-stick page mapping after keyboard dispatch and
bounded metadata drawing without changing approved/current record comparison.
Manual Scan refuses busy catalogue operations. Menu entry follows the primary's
unconditional native rescan; the catalogue worker does not access modlist. Existing
completed-install Modlist_Init remains, avoiding an unrelated draw-callback change.
The Scan drawn/hit area shares native row144; desktop ticker/arrows remain. Scoped
whitespace checks passed. No builds/tests, rendered UI or controller trials ran;
the acceptance cases above remain deferred.
