# Numbered PAK case compatibility

Verified: installed Zerstorer (`zer`) has PAK0.PAK/PAK1.PAK;
PAK0.PAK contains progs/v_cgun.mdl and maps/start.bsp. Its loose progs.dat
loads while current lowercase-only COM_AddGameDirectoryRoot misses both packs,
so the client cannot complete model precaching. This is a general filename
compatibility issue, not missing game assets or a model substitution problem.

Reuse the inherited main reference COM_FindNumberedPack policy: exact lowercase
first, then deterministic case-insensitive match in the same directory, files
only. Adapt enumeration to vkQuake's existing Sys_FindFirst/Next API to retain
its platform/allocator ownership. Keep consecutive pack numbering, mount
priority, pack content lookup, game roots and loose file lookup unchanged.
Reuse the same helper for the mod catalogue's primary-pack presence check.
Do not rename installed files, add Zerstorer-specific aliases or replace VFS.

Verification: actual Zerstorer local signon and model lookup on Linux; focused
production resolver checks uppercase/mixed case, exact-name precedence,
deterministic duplicate selection, directories rejected, and missing numbering.

The actual initialized Linux desktop `zer/start` client now completes signon4
and loads `progs/v_cgun.mdl` at precache slot44. Installed PAK names and payloads
were preserved. Evidence: `zer-final/result.json`/`signon.log` in the current
external hotfix diagnostic root.
