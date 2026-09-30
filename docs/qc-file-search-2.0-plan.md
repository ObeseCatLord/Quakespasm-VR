# Inherited QuakeC file-search adapter

## Reference and current state

Primary `Quake/pr_cmds.c` implements search 444-447 with 16 VM-owned snapshots,
mounted-path traversal, first-result case-insensitive deduplication and its own
wildcard matcher. Empty searches return -1; invalid handles return zero/empty;
`search_end` and VM shutdown free snapshots. The primary signature includes
flags, quiet and package-filter arguments, but its implementation ignores them.
Preserve that inherited contract; do not claim QSS-M package-filter support.

QSS-M's `pr_ext.c` uses the same four slots and snapshot/lifecycle organization,
but depends on `COM_ListAllFiles`, a broader filesystem implementation absent
from destination. Importing that filesystem would replace adjacent working
systems just to support this API. Destination already has mounted pack tables,
Ironwail-derived portable `Sys_FindFirst/Next/Close`, native file/path helpers and
`PR_ShutdownExtensions`. These remain authoritative.

## Minimal implementation

Copy the primary wildcard matcher and snapshot/handle organization into
`Quake/pr_ext.c`. Use native `Mem_*` allocation and an array of names; primary's
stored file size/searchpath metadata has no consumer among these four APIs.
Do not retain mounted-path pointers across game-directory changes. Preserve
discovery order and case-insensitive deduplication.

Adapt only loose-directory enumeration to `Sys_FindFirst(dir, NULL)` and
`Sys_FindNext`; it already owns Windows UTF-8 and Linux/ARM directory resources.
Filter basename patterns with the copied matcher and preserve nonrecursive
loose-directory behavior. Keep regular-file/readability checks through
`Sys_FileType`/`Sys_fopen`, closing each check immediately. No unused size seek
or direct platform enumeration implementation. Use bounded path construction;
omit names that cannot fit the engine's existing `MAX_QPATH` ABI instead of
returning a truncated name that identifies a different file.

Preserve mounted pack traversal. Destination's special rerelease model-only
mount must obey the same `COM_IsRereleaseModelAsset` predicate as normal reads.
Expose that existing predicate through `common.h`, retaining its implementation
and policy. Do not copy its extension list into QC or mount another filesystem.

Validate search patterns through existing `QC_FixFileName` rejection rules,
but query the original mounted-relative pattern: discovery returns filenames,
not file contents, and must not prepend `data/` or change asset-name matching.
This intentionally retains donor's stricter '..' rejection. The existing file
read APIs retain their own data-path/config-read rules.

Add finite/bounded validation before numeric handle/index conversion. Reuse
primary's positive fractional truncation behavior. Wire snapshot shutdown into
the existing per-VM `PR_ShutdownExtensions` owner. Register 444-447 for both VMs
and the inherited `DP_QC_SEARCH` capability after all four handlers exist.
Other capability case/disable issues remain separately tracked.

## Scope, alternatives and proof

Production ownership: new private QC search section and registrations/shutdown
in `Quake/pr_ext.c`, existing predicate visibility in `common.c/common.h`.
Estimated scope about 200 lines; no changes to mounts, file resolution, VM
dispatch, renderer or platform enumerators. Native `wildcmp` cannot replace
primary's matcher because its `*` stops at directory separators while primary's
does not; retain the copied helper rather than changing adjacent string APIs.

The smallest end-to-end proof, at final implementation-wide software checking,
is an SSQC/CSQC search over a mounted pack and a loose folder, with overlapping
names, querying filenames/counts and closing/reloading the owning VM. Include
zero results, 16-handle exhaustion/reuse, independent VMs, cross-VM/closed and
nonfinite handles, malformed/long paths, nested-pack wildcard behavior,
nonrecursive loose patterns, regular/readable-file filtering, model-only mount
visibility and filesystem precedence. Windows builds, hardware and performance
measurements remain deferred. No tests/builds/compiler or runtime probes until
all implementation is finished. Personal local Astra source review checks the
actual adapter and lifecycle before commit; broad MOD completion is not implied.

## Windows wildcard review correction

Personal local Astra found a P2: primary passes loose-file patterns directly to
Windows enumeration. Post-filtering native results with the Quake matcher would
drop extensionless files from `*.*`. Microsoft's [FindFirstFileW documentation](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfilew)
and [wildcard compatibility explanation](https://devblogs.microsoft.com/oldnewthing/20071217-00/?p=24143)
describe the native matching boundary and DOS compatibility rules. The matching
rules differ beyond one literal pattern, so special-casing only `*.*` would not
fully preserve this boundary.

Adapt the existing Windows enumerator: expose `Sys_FindFirstPattern(dir, pattern)`
using its current UTF-8 conversion, find-data storage, iteration and close owners.
Keep `Sys_FindFirst(dir, ext)` as its existing extension-pattern wrapper. QC uses
the native pattern entrypoint on Windows and skips the Quake post-filter there;
Linux/ARM retain native directory enumeration plus the copied primary matcher.
Pack matching remains unchanged on every platform. This adds one Windows-only
declaration/entrypoint in `sys.h/sys_sdl_win.c`, without copying the Windows file
API implementation or changing ordinary enumeration semantics. Reopen the source
review for this bounded adapter. Windows execution remains deferred; final checks
should include `*.*`, extensionless names, extension patterns and native short-name
matching in addition to Linux/ARM cases.

Main's reference spot-check also confirms that primary's readable-regular-file
check is POSIX-only. Windows uses native find attributes without opening each
result. Keep that platform boundary in the adapter: POSIX retains its type/open
checks; Windows retains native enumeration and directory filtering. The earlier
generic readability description must not impose a new Windows filter.

## Source acceptance

The adapter is implemented. Personal local Astra Max accepted the final source
with no remaining introduced P1/P2. It verified owned snapshots, deduplication,
mounted precedence, finite/bounded conversions, VM ownership, exhaustion and
shutdown, shared model-only filtering, both-VM handlers and capability.
The Windows P2 is resolved through the existing native pattern/iteration owner;
ordinary extension enumeration keeps its output. POSIX and pack matching remain
the inherited implementations. Main retains the copied primary wildcard body,
and no new filesystem or platform resource owner is introduced.

Astra also validated the routine `strconv` 249 alias registration: both VM
handlers reuse 224's existing converter, with named discovery/binding still
choosing 224. Main's earlier wrapper/helper comparison covers its conversion
contract. No builds, tests, compiler/runtime or performance probes were run.
Windows execution remains deferred. Ignored search parameters and bounded path
changes remain explicit; this source acceptance does not close all MOD features.
