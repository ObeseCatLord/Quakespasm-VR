# q30 real-BSP roomscale/liquid traversal

Status: plan committed before implementation; bounded airborne fixture stages
pass Linux checks and final local Astra review. This addresses the actual-traversal stage of
the [q30 transition plan](predictive-q30-transitions-2.0-plan.md), retaining normal
admission/full serialization/parser/replay and wider AD-family requirements.

## Behavior, reference and current evidence

A desktop or VR player crossing into liquid must keep the inherited QC/native
input order, body motion and completion, without a repeated callback, extra world
interval or lost later queue head. Actual roomscale entry must choose the existing
fresh native owner before command staging; a later unstarted head must remain
queued for the next native world frame. Ordinary dry movement stays predicted.

Verified current implementation: SV_PrivateWalkTrialQ30NeedsNative reuses the
actual callback-free auxiliary sweep and restores player body/command/water/link/
PVS before selecting dispatch. Existing fixtures prove restoration, a prepared
wet/native comparison and a prepared retained-head boundary. They do not prove
horizontal dry-to-wet traversal. The shared FindLiquidPosition returns only the
first qualified sample; previous searches failed to find an accessible dry
neighbor at their selected samples/maps. That is a fixture search limit, not
evidence that the native owner or map collision should be rewritten.

## Reuse and minimal adapter

Extend the existing fixture-only liquid finder with a caller-owned match ordinal,
as already authorized by the parent plan. Preserve its exact leaf/sample order,
real hull/content tests, restoration, and old first-match wrapper. Reuse it to
select later wet positions; add no second BSP search, collision solver or cached
candidate lifecycle. Check bounded neighboring dry origins using existing native
traces/contents and the actual roomscale sweep. This is verification code, not a
gameplay movement or geometry policy. No generated map or modified game asset is
the reference.

Use the existing native-engine bootstrap with an explicit fixture map option;
e1m1 remains the default for current checks. A different shipped BSP may be used
when its geometry qualifies. Prepared airborne starts must be identified as such;
do not call them grounded shore traversal, authored inventory or normal signon.
Do not suppress a failed traversal check and label the feature qualified.

Rejected: another water sampler/standalone bootstrap, fabricated geometry as
proof of the shipped map, late ClientThink, extra native interval after earlier
heads, or weakening eligibility simply to make a fixture pass.

## Stages and ownership

1. Extend tests/native_liquid_fixture.h with ordinal selection and retain the
   stock first-result wrapper. Verify original lookup/restoration is unchanged.
2. Add a separate traversal mode to tests/q30_movement_native_fixture.c using the
   existing bootstrap, optional map argument and bounded dry-neighbor search.
   Prove actual dry start, unobstructed swept entry, changed liquid observation,
   and full probe restoration on the selected real BSP. Keep prepared-body limits
   explicit. Preserve the ordinary fixture modes and map default.
3. Compare actual native versus selected roomscale/QC/physics/completion from
   the same qualified body and nonzero analog input. Verify one completion, zero
   native credit and queue retirement. Compose a real later-head crossing when
   geometry permits: complete the earlier dry head, retain the unstarted crossing
   head, then consume it once through the existing fresh native owner without
   new command-time credit. A helper-only probe is not the final behavior proof.
4. Run consolidated Linux checks and local Astra integration review; record
   demonstrated failures before changing production. Complete remaining callback
   scheduling and normal admission/publication/replay in their existing plans.

Exact current write set: the two fixture files above, tests/README.md and linked
planning/review records. Existing makefiles already depend on the shared header.
Expected production changes: none. Reopen and commit the contract before a
demonstrated production fix or another geometry/movement owner is required.
Hardware, performance measurement and Windows/ARM qualification stay deferred.

## Qualification and final review

The shared ordinal finder and optional-map traversal mode are implemented.
The shipped1024_tango BSP (SHA256
d08fe272f025fa6c30ac87609ab6c8cb6183a0351578ea9cb17eca28612098c1) qualifies
water/depth1 ordinal45 with a48-unit horizontal sweep from136,-516,428 to
136,-564,428. Actual dry start, clear hull, swept water observation and exact
probe body/link/PVS/command/duration restoration pass. Nonzero analog input
retains fresh native/selected motion, flags, health and completion agreement.

The actual selected8ms dry prefix matches the single-head reference. Its raw
30ms crossing head remains queued unchanged; the next10ms native world interval
consumes it once with zero credit and matching final body/completion, and clears
room delta in retired history. e1m1/e1m2/e1m4 searches found no qualifying dry
neighbor; those failures remain geometry/search limits rather than passes.

A fresh read-only local Astra Max reviewer found no blocking issue. Main
independently verified final effective `gpt-6-astra` / `max` settings, the
unchanged first-wrapper algorithm and the timing/selection/completion assertions.
The reviewer inspected supplied artifacts without running checks.

| Final review finding | Disposition |
| --- | --- |
| Ordinal enumeration preserves sampling/restoration and first-match behavior. | **Verified/adopted.** Retain the single shared finder; no parallel sampler or cached lifecycle. |
| Native reference and retained-head case use equivalent QC/world opportunities. | **Verified/adopted.** Reference explicitly disables selection after its actual selected prefix. Pending duration30ms does not add native command time to the next10ms world interval. |
| Proof is the prepared airborne component case on1024_tango. | **Adopted.** Grounded shoreline, all-world actor restoration, general callback multiplicity, normal admission/wire/replay and wider mods remain outside this evidence. |

Linux assertion-enabled SDL3/Werror fixture build and traversal exit0; normal
q30 movement/replay, scheduled-camera and empty-ammo modes also exit0 with their
aggregate markers. Production remains unchanged by this slice. The separate
[scheduling audit](predictive-q30-callback-closure-review.md) closes its bounded
normally initialized client/target scope without another production gate.
Normal session admission/full publication/parser/replay and the wider AD-family
goal remain incomplete; this record is not a migration completion certificate.
