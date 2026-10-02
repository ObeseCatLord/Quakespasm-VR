# Loaded entity copy and player queries: final qualification plan

2026-10-01. Finite F03 boundary; no GPU work. Main verified current native
pr_ext.c4145/4171/5679 against readonly primary pr_cmds.c5851/5871/3002,
native ED_Alloc/Free/RebuildFreeList, edict layout/stride, current-VM SV_LinkEdict,
actual CL_LoadCSProgs and native scoreboard sorting/allocation/freeing. Existing
[source plan/repairs](qc-entity-copy-player-query-2.0-plan.md) retain native owners;
current production already includes the payload/free-source preflight/bounds fixes.
No additional demonstrated incompatibility calls for another allocation,
entity, world or scoreboard service. Final source review follows real receipts.

Reuse the licensed-program assembler/native fixture and unchanged host adapter.
Separate trailing assemble copy_player=False / CLI --copy-player / runtime
-copy-player, mutually exclusive with previous modes. Retain original six
prefixes/version/CRC; append9 declared QC words: fixture_copy_tail_scalar(float),
tail_vector(vector3), tail_string(string), tail_entity(entity), tail_function
(function), tail_field(field), tail_last(float). Prefix all with fixture_copy_.
Twelve previous outputs/variants must remain byte-identical. Both generated
SSQC/CSQC programs identical; no-op CSQC_Ent_Update for real native admission.

Exact refs fixture_ref_copy_explicit, fixture_ref_copy_allocate,
fixture_ref_copy_edict, fixture_ref_copy_player_string,
fixture_ref_copy_player_float, fixture_ref_copy_permission and
fixture_ref_copy_touch. Builtin ref namespace
copy_builtin_NAME must be separate. Native copyentity400, edict_num459,
getplayerkeyvalue348, named getplayerkeyfloat, named checkbuiltin, strlen114,
strcat115. Inputs fixture_copy_input_source/destination(entity), index(float),
prime_index(float), player(float), key(string). Outputs fixture_copy_entity(entity),
prime_entity(entity), scalar(float), prime_scalar(float), string(string),
prime_string(string), player_enabled(float), player_float_enabled(float),
touch_count(float). Prime native edict_num on a live nonworld input before
copy/edict result; strlen/strcat(copy-player-prime) before player queries.
Record exact nonzero primers; no C-assigned OFS_RETURN. Permission caller
checkbuiltin verifies both client-only query declarations in each VM. Touch QC
increments count and decrements actual global-other.health using original fields.

Native C under QC_COPY_PLAYER_NATIVE_HOST_FIXTURE. Extend existing host declaration
gate only; no new owner adapter. Do not modify previous fixture behavior. Actual
native client loader/area nodes/heap, same read-only real world model inputs.

Both VMs: live source/destination/native omitted-destination allocation, exact
returned encoding and full QC payload including declared typed tail and actual
merged terminal words. Initialize valid payload, not arbitrary invalid references.
Compare source unchanged and destination payload, accounting only for native
absmin/absmax recomputation. Set a nonzero witness at the final actual QC word
using its float/vector-component metadata. Exact alpha/sendinterval/default copy;
destination native debug identity, freetime/retain count/baseline stay independent.
Adjacent live edict payload/header snapshot guards boundary writes. Self-copy
must still relink stale spatial state and retain metadata/payload.

Observe real current-VM area-list ownership before/after copy between opposite
root-node sides, native box bounds and no-touch count/health. Link a trigger with
loaded touch callback at the copied position; copy must not touch. Explicit
native SV_LinkEdict(dst,true) positive control fires once (both VMs if native
ownership supports it). Avoid stock-world collisions with prepared outside-world
Z. Reuse area-list observation logic rather than copying a world implementation.

Capture encoded source/destination while live, then native ED_Free. Free source
with explicit destination must not mutate it; free destination must remain free
and unchanged. Free-source omitted destination must return world without changing
num_edicts, native FIFO/head/state, source payload or allocations. Make source
earliest reusable head via prepared freetime and native ED_RebuildFreeList(false).
For edict-limit preflight, drain native FIFO via force-rebuild/ED_Alloc, free source
with nonreusable future freetime and set max_edicts=num_edicts temporarily;
rejected copy must still return without allocation/error. Restore limit/time
metadata afterward. Do not change native heap/free-list or install an alloc hook.
Numeric edict bounds: -1,0,last-live,count,count+7, with nonzero primer; malformed
raw refs/nonfinite conversion/live free-index debug policy remain excluded.

CSQC queries only. Prepare exactly3 score records using native Mem_Alloc (native
serverinfo's allocation formula), one empty slot, named slots0/2 with distinct
frags/ping/entertime/colors, and native Info_SetKey extra numeric/text fields.
Call real Sbar_SortFrags: negative -1/-2 follow native ordering; -3 and sorted
selection mapping outside allocation refuse. Names/viewentity (including empty
slot), frags/ping/entertime/top/bottom/team, RGB from loaded native palette and
extra userinfo pass string/float conversions; empty/unknown/pl/userid/missing/null
scoreboard/count/MAX_SCOREBOARD cases replace nonzero primers with raw0/float0.
Restore score state; no fictitious oversize server allocation or alternative sorter.

Two actual client loads/clears preserve a linked SSQC entity/payload/metadata;
queries/copies repeat after native server replacement. Actual CL_FreeState frees
the prepared native scoreboard and clears client VM at the end (only null native
graphics allocations exist); then native client reload queries absent scoreboard.
If an unexpected renderer boundary is reachable, report rather than launch it or
replace reset ownership. Prepared scoreboard inputs do not certify full graphical
serverinfo parsing, authored HUD, network cross-play, shallow zoned-string lifetime,
error unwind, ARM/Windows/hardware/performance/full F03/F10.

One Luna/xhigh source-only worker owns tests/qc_binding_program.py and
tests/qc_binding_native_fixture.c (tightly coupled), <=160 Python/<=420 C added
lines. Main owns docs/private recipes/integration. Not alone: no reverting others,
no delegation/production/other files/branch/commit/runtime/build/test actions;
diff-check only. Report before cap expansion or missing evidence. Main reviews
completed source, then strict CPU qualification and bounded local Astra review
of allocator/spatial/oracle/lifetime evidence; final affected checks/receipts and
explicit commit on2.0. Existing user migration-doc edits remain untouched.
