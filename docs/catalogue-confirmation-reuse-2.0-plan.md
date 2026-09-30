# Catalogue confirmation through the existing installer

2026-09-30. Before-code plan for a narrow UI-003 integration cleanup. This is
not a new installer or approval flow. Keep the current Mods details page,
keyboard/controller/VR pointer dispatch and explicit confirmation behavior.

## Verified source and incremental choice

`menu.c:M_Mods_ConfirmCatalogueInstall` retains the entry displayed on the
details page, resolves its current index and checks it using a private duplicate
`M_Mods_CatalogueEntryMatches`. It then starts installation with the index-only
API. The existing `addon_catalog.c:AddonCatalog_EntryMatchesApproved` expresses
the same field policy with bounded strings; `AddonCatalog_StartInstallApproved`
rechecks that snapshot under the installer mutex before copying the job. The
server-mod path already uses both functions at `cl_main.c:923/940`.

Reuse those two functions in the Mods confirmation path and delete its private
comparator. Keep early page feedback and installed-entry selection. This
centralizes the existing comparison and job-copy boundary, without claiming a
reproducible race or inventing generations, a second mutex or download policy.
The displayed entry remains the approval snapshot; `allow_unverified=true`
continues only on the existing explicit confirmation action.

Write scope: `Quake/menu.c` only, at most20 changed production lines. No API,
catalogue worker, filesystem, renderer, protocol, page-layout or message edits.
Reopen before expanding ownership. A bounded local coding agent implements;
main reviews complete diff and exact producers/consumers before committing.

## Qualification at the end

No builds/tests/downloads/probes now. At consolidated Linux/ARM qualification,
verify displayed-entry confirmation, changed/missing entry refusal, installed
selection, unverified confirmation, startup failure feedback and ordinary
keyboard/controller/pointer behavior. Retain existing cancellation, validated
PACK/temp/rename, retry and server-mod reconnect coverage in the full UI-003/004
matrix. Source review alone is not installation or runtime qualification.
