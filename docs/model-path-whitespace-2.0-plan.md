# C06/NET-007 model-path delta after the frozen audit

2026-10-01. Read-only primary reference HEAD is now
7acafa8b1c5bb7c2c7de17b2650d93d8bdb60f5f, one commit after canonical51b452c0.
The only source delta is24 additions in Quake/gl_model.c: Mod_ForName resolves
trailing space/tab spelling when the exact virtual file is absent and the
trimmed one exists. Main inspected the complete diff and native2.0 Mod_ForName
1204: only Mod_FindName then Mod_LoadModel, so this inherited correction is absent.
This is a bounded follow-up at existing C06/NET-007 resource compatibility,
not another independent feature or expansion of the185-row frozen inventory.

Copy the existing primary code into native Mod_ForName before Mod_FindName.
Retain its bounded MAX_QPATH stack buffer, nonempty/short path admission,
space/tab-only trailing removal, exact-file precedence and trimmed-exists gate.
Keep native model cache/load owner and stack-to-cache name copy. Do not change
QuakeC strings, precache/network names, filesystem search paths, crash behavior,
unrelated loaders or introduce a mod-name special case. Invalid/oversized/empty
names and inline/generated paths retain native handling unless both original
file-lookup gates explicitly admit fallback. No general filename normalizer.

Existing Mod_FindName copies the selected spelling into model-owned storage;
Mod_ForName is shared by server precache and client ordinary/dynamic precache.
The adapter reuses these owners rather than duplicating filename/cache policy in
client/server/QC callers. No new metadata/signon/renderer resource owner.

One Luna/xhigh worker, exclusive Quake/gl_model.c Mod_ForName only. It is not
alone; metadata worker owns seven other files, main owns docs/integration.
Target24–35 changed lines; stop before50/new helper/module. Preserve other work,
no stage/commit/branch/ref changes. Source reads and scoped diff check only;
no tests/builds/compiler/lint/syntax probes/fixtures/scripts/SSH/games/telemetry.
Report scope_done, files/count, source verification, assumptions/risks/follow-up
and relinquishment. Main reviews/commits before final qualification starts.

Deferred meaningful cases at actual native filesystem/cache owner: exact file
with legitimate trailing whitespace wins; absent exact/existing trimmed file
loads and shares canonical cache; missing both retains existing failure/cancel
path; spaces/tabs/non-trailing whitespace, length/empty/inline and repeated load,
server/client/dynamic precache, ordinary native desktop/VR. All execution waits
until required implementation is finished; source copying is not runtime proof.

Source-integrated in c034a2f5 after main review of Luna's24-addition/no-deletion
diff. Only Mod_ForName changed; bounded buffer/space-tab gates, exact-file first,
trimmed-file existence and synchronous model-owned cache copy checked. Luna
routing was externally verified gpt-6-luna/xhigh; completed worker closed. Scoped
diff check passed; no executable checks. Deferred cases above remain required.
