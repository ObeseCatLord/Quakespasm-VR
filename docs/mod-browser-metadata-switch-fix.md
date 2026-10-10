# Installed mod names after switching games

## Evidence and plan

The installed list still contains distinct game directories. The repeated names
come from `Modlist_Init`: it loads the active search path's `mapdb.json`, then
allows that mod's first episode name to replace every otherwise unnamed mod.
Opening the browser rescans this metadata, so selecting The Immortal Lock makes
unrelated entries appear to disappear behind the same display name. This is a
shared metadata ownership bug, not an Immortal-specific menu rule.

Preserve the existing scanner, menu, metadata precedence and directory identity.
Use the file's existing search-path identifier to identify its owning game
directory. Permit renamed-directory fallback only for that owner; other entries
require an explicit matching episode directory. Preserve base-game episode
matching and the Copper rule that avoids relabeling Copper-derived mods.

The sorted/filtered browser vectors must also be checked for lifetime errors
across an actual `playgame` switch; do not add another list/cache architecture.

After implementation, verify matching/nonmatching/renamed/base/dependency cases,
then reproduce search -> choose Immortal -> reopen -> search another mod on an
isolated software-Vulkan desktop display. Installed assets remain read-only and
the test uses a private user directory. Build/package/deploy via the existing
release workflow after the fix passes. Main stays untouched.

## Results

The fix resolves the existing `mapdb.json` path identifier to its search path's
`dir`, and enables mismatched/renamed-directory fallback only for that owner.
It adds no persistent state, menu implementation, or mod-specific exception.
The browser vectors are cleared before its existing scanner frees/rebuilds the
installed nodes; normal game switching does not free the list independently.

Production Modlist/JSON-source fixtures pass under ASan/UBSan. They cover owner
and non-owner names, case-insensitive matching, base episodes, Copper-derived
metadata, dependency ownership, overlays, localization and existing metadata
precedence. The same fixture rejects the old implementation.

The native desktop regression uses the actual menu action, character input,
Enter selection, queued `playgame`, Immortal's start map and rendered menu.
Thirty-nine installed directories are represented by a private asset overlay;
configs are copied into that overlay, and installed asset/config files are not
written. Software Vulkan runs on a private Xvfb display without physical input.

The old executable reproduces the reported sequence: 35 unrelated entries are
renamed, producing 36 "The Immortal Lock" rows. The fixed executable preserves
all 39 directory identities and their original display names after switching,
three further reopen/scan cycles, a Peril search and another Immortal search.
The reopened rendered menu was visually inspected and contains one Immortal
entry with the other mod names intact. Native build and whitespace checks pass.
