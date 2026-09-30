# Native QC file-content failure repairs

2026-09-30. Before-code brief following the requested local Astra read-only
content audit. Effective settings/runtime execution remain unverified. Main
spot-checked all three source deductions against current code and actual primary.
Previously repaired ownership, append/mkdir and cached position are not reopened.

| Defect and actual source | Minimal correction |
| --- | --- |
| PF_fseek pr_ext.c:3621 commits requested logical offset and cache invalidation even when Sys_fseek fails; native Unix sys_sdl_unix.c:62 returns fseeko status. | Compute target locally. Commit fileoffset/cache reset only on successful seek. Preserve prior-position return, filebase and existing raw-int seek API. No new offset policy. |
| PF_fgets pr_ext.c:3545 consumes an overlong physical line while retaining a prefix; its last retained CR is stripped as though it were the real line ending. | Track actual output truncation. Strip trailing CR only when the retained result reached the physical line end. Preserve native whole-line consumption, capacity and blank/EOF behavior. |
| PF_buf_loadfile trusts COM_LoadFile, which ignores Sys_FileRead count in common.c:2806; unread allocation contents may be parsed after a release-build short read. | Pending bounded disposition below; preserve embedded/native VFS reading and primary failure semantics, not a separate QC filesystem. |

## Checked-load decision reopened before coding

A QC-local COM_FOpenFile/fread temporary loader looks smaller than a service
rewrite, but main verified a concrete incompatibility: native embedded
vkquake.pak is a memory-only Sys_MemFileOpenRead pack at common.c:3302–3321.
COM_FOpenFile's stream branch reopens a physical filename; switching from
COM_LoadFile's native duplicate handle loses that memory-backed path. Do not
replace the native loader merely to obtain a count.

Primary51b452c0 common.c:2163 already captures Sys_FileRead count, closes the
handle and uses native Sys_Error on an incomplete read. Lean: copy exactly that
check at current COM_LoadFile, preserving existing Mem_AllocNonZero, VFS memory/
file/PAK resolution, zero terminator and fatal loader policy. This is a repair
at an existing owner, not a new loader. The audit's suggested builtin failure0
is not the primary short-read contract; do not silently claim graceful QC-only
error semantics. Debug Sys_FileRead may assert before this check; release no
longer consumes unread bytes. A bounded reviewer disposition precedes this
common.c change. No allocation/table/registry/VM or file API replacement.

Expected write set pr_ext.c (two wrappers) and common.c (checked native read);
under20 net production lines. Reopen for a new helper/policy or more ownership.
Absent a concrete further defect, fputs/writefile formatting/ranges/newlines
and existing native buffer load line parsing remain. Intentional canonical
data/ paths, capacities, append, seek returns and resource ownership remain.

After all implementation, Linux/ARM software checks cover failed loose-file
seek with unread cache, successful seek/filebase accounting, long line prefix
containing an interior CR and ordinary CRLF/blank/EOF, and checked reads through
loose, disk PAK and embedded-memory native loader. Use isolated malformed/short
files for expected native failure, not game assets. Cover buffer-load append,
blank/final lines and unchanged writes/ranges. No builds/tests/compiler/probes/
fixtures/benchmarks before implementation closes.

## Two-wrapper source integration checkpoint

PF_fseek now holds the requested offset locally and updates logical position/
read-cache state only after Sys_fseek returns success. Its prior-position return
is unchanged. PF_fgets records actual output truncation before consuming the
remainder and only removes CR from an untruncated physical line ending. Main
reviewed both complete diffs and the native fseeko wrapper/cache loop; scoped
git diff --check passes. Seven net lines in pr_ext.c; no executable checks.
The checked native-loader disposition remains pending before common.c edits.
