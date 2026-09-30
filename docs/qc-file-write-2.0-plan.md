# QuakeC append and parent-directory compatibility

2026-09-30; plan before code on2.0. Goal: mod fopen(mode1) appends without
destroying existing content; mode1/2 can create nested parent directories as
the inherited VR implementation does. Both desktop and VR use the same native
QC file service, with no new service, filesystem owner or permission policy.

## Verified evidence / reuse decision

* Primary master51b452c0 `pr_cmds.c:5130-5155` uses append `ab` / write `wb`,
  calls its small `PF_QCCreatePath` slash-walk helper, and then registers the
  stream in its table. The helper at5092 calls Sys_mkdir for each parent only.
* Current destination `pr_ext.c:3402-3467` uses `Sys_fopen(name,"w+b")` for
  mode1, seeks to end, and subsequently stores native filebase/owner/cache state.
  Seeking after a truncating open cannot preserve original content. Mode1/2
  currently do not create parent directories.
* Pinned native vkQuake and QSS-M retain the truncating append open; do not copy
  that defect. QSS-M's mode2 has a similar parent slash walk, but mode1's mkdir
  of the whole file path is not the behavior to import.
* Destination `QC_FixFileName` rejects invalid names and normalizes writes under
  data/. Keep this native normalization/read-only fallback contract and current
  owning-VM handle guards, Sys_fopen portability wrapper, dynamic file table,
  cached reads, native EOF handling, teardown and fseek additional API.

These are direct source reads, not execution evidence. The minimal adapter is
the primary's parent helper at the existing fopen boundary and its append mode
string. Replacing cached file I/O, adopting the primary fixed32 table, broadening
write locations or copying its byte-at-a-time reader would duplicate working
owners and is unnecessary. Native data/ paths deliberately remain canonical;
this does not preserve primary's unrestricted relative write location policy.

## Exact implementation

Write set only `Quake/pr_ext.c`. Copy the small primary PF_QCCreatePath helper
as one private QC path utility. In mode1 and mode2, after building the already
normalized full path, call it before Sys_fopen. Change mode1 to `ab`, retaining
the native end seek and filebase assignment. Mode2 stays truncating `wb`; all
other handlers and modes remain. Append-mode writes always land at EOF even
after fseek, consistent with the inherited append contract; the native fseek
query/position calculations remain untouched. No new callback/registry/state.
Expected under20 added production lines; reopen if additional storage or policy
is needed. Main reviews/integrates a single bounded coding worker's output.

No builds/tests/compiler checks/probes/fixtures until full implementation. Only
source reads and scoped whitespace checks now. Final Linux/ARM software checks
must cover append across reopen without losing original bytes; creating a fresh
nested file; mode2 intentional replacement; failed/denied paths; readonly PAK
reads and current-VM handles; append-after-seek versus native positional mode2
writing; shutdown/reload file retirement. No real game files are modified for
this slice. Source parity alone does not qualify complete MOD-001/005 behavior.

## Source implementation checkpoint

`06cc04ea` adds the primary parent helper and changes exactly the two native
write cases (18 insertions, one deletion). Main compared the actual diff with
the inherited helper/open mode and confirmed directory creation follows native
name normalization, keeps Sys_fopen and does not alter handle/cache owners.
Scoped `git diff --check` passed; the coding worker is closed. No execution or
real file operations were performed.

One bounded local Astra source audit may compare the four primitive inherited
file wrappers/registrations (fopen110, fclose111, fgets112, fputs113) and native
cached-read/teardown/fseek boundary with primary51b452c0. The native canonical
data/ path policy, dynamic table and whole-line read capacity are deliberate
reuse decisions; do not replace them merely for byte-at-a-time source matching.
Report actual call/signature/permission/lifetime gaps with evidence separately
from intentional differences and remaining execution unknowns. No builds,
tests, probes, nested agents, telemetry or edits. This is a source audit, not a
new architecture decision or certified senior-skill gate.

## Bounded source audit and correction plan

Requested local Astra Max inspected the integrated append/directory adapter and
the retained native file service at27ba9c8d. Effective settings metadata was not
exposed; this is a source advisory, not certified review or runtime qualification.
Main spot-checked the two actionable findings against the current implementation:

| Finding / evidence | Disposition before code |
| --- | --- |
| PF_fopen grows qcfiles with Mem_Realloc and reads the new slot's file before initialization; mem.c:120 does not zero realloc growth | Adopt: memset exactly the added slot immediately after growth, before occupancy inspection. Keep native dynamic capacity, VM ownership and handle numbering. |
| Read refills advance fileoffset by all fetched bytes, while cacheoffset tracks consumption; PF_fseek returns the refill endpoint | Adopt: read-mode prior-position query becomes fileoffset - cachesize + cacheoffset. Keep native PAK filebase, refill accounting, seek/reset and write-mode positions. |

The append/directory patch introduced neither defect. This supersedes the earlier
instruction to leave native seek calculations untouched only for this confirmed
read-position error. One bounded worker owns only Quake/pr_ext.c: one slot memset
and one read-position expression/comment (under six changed production lines).
No alternative table, per-character reader, path-policy change or new file owner.
Reopen if more state/policy is needed; main reviews the diff before commit.

Other advisory comparisons accept the ordinary 110-113 signatures/VM dispatch,
append/replace modes, denied/failed-open behavior, PAK bounds and owner teardown
at source level. Deliberate native differences remain: canonical data/ writes,
restricted read fallback, whole-line consumption with bounded output, interior
CR preservation and an empty non-null final blank line. Primary's fixed table
and byte-at-a-time reader are not copied. Failed seeks, malformed/oversized inputs
and allocation exhaustion are not qualified by this bounded audit.

Add table growth/reuse and cached read-position save/restore to the final isolated
Linux/ARM checks, alongside cache/PAK boundaries and existing cases. No tests,
builds, compiler checks, probes or actual file writes until full implementation.

### Correction integration checkpoint

The bounded coding worker added exactly the planned slot memset and corrected
read-position expression/comment. Main inspected the two-hunk diff and signed
long-long cache/offset types; scoped git diff --check passed. The worker is closed.
The correction keeps all existing handles, ownership, seek/reset, native paths,
append behavior and filebase accounting. No execution was performed; this is
main source acceptance of the advisory's narrow repairs, not a further certified
review or complete file-service qualification.
