# Inherited pickup target and item respawn adapters

2026-09-30. Before-code verified brief for COOP-004/005. Desktop and VR share
native QuakeC touch/target/item scheduling. Revival is excluded. No build/test,
compiler/probe/fixture or benchmark until full implementation is finished.

## Goal, actual evidence and environment

Solo-maintainer engine port: copy working inherited policy into native owners,
without another inventory, target engine, timer queue, mod registry or service.

| Fact | Source verification / consequence |
| --- | --- |
| Reference master51b452c0 has pickup-target repair and supplemental regeneration. | `world.c:310–648,2997–3100`; target unchanged/accepted-gain gates, SUB_UseTargets and native SUB_regen callbacks. Exact controls/defaults/flags are in `sv_main.c:153–161`, registration`:1134–1143`. |
| Actual2.0 native touch owner lacks these policies. | Main/Astra direct reads `world.c:2441–2548`: native retained list/edict callbacks and accepted sharing are present; no target snapshot/dispatch or item scheduler/cvars. A stale CSV label is not the evidence. |
| Existing inventory captures/gain predicates already match the reference. | `world.c:1805,1973` and current SV_ShareCoopPickupInventory. Reuse shared_before/shared_after when sharing is enabled; only capture extra when target repair needs them independently. Do not duplicate inventory schemas or add another gain policy. |
| Native field/function helpers exist. | ED_FindField + masked type + GetEdictFieldValue; ED_FindGlobal activator entity; existing positive in-range QC-body admission in SV_CoopCallKeyFunction. Adapt primary name-only helpers to typed strings and correct native progscrc. Reuse SV_IsDirectWeaponTouch rather than adding its cache again. |
| Native retained edicts prevent ordinary item reuse but not reserved player recreation. | Actual native PF_dropclient/PF_spawnclient (`pr_ext.c:2468–2506`) can recreate the same client/edict addresses. Existing SV_CoopRespawnBindPolicy/Unbind/OwnerLive and drop/connect/free hooks already provide sticky cancellation. Do not add generations or a second registry. |
| Current private callback helper isolates input/call/context/basis/trace. | `sv_phys.c:9247–9331` already borrows the original client owner. Its QC self/other are hardcoded to that client/world, and its return is restored before callers can observe it. Narrow adaptation is needed for target-self/player-other or selector-result reuse; no actual VM teardown can return normally through these calls. |
| Defaults/class selection are inherited generic policy. | Weapon target fix -1: modern1/classic0; explicit2 accepts custom weapon touch. Nonweapon target fix0 with empty class list, optional log0. Ammo respawn-1/time30; progression respawn-1/time5, inherited boot/suit/scuba classname conventions/class control. No additional per-mod special cases. |
| Runtime effects and repeated pickup/target counts are unknown. | Source admission or a callback packet does not prove gameplay. Final Linux/ARM acceptance is required; user hardware/performance trials remain excluded. |

## Behavioral contract

1. Preserve original native touch first. For eligible trigger weapons or
   explicit configured item/ammo classes, capture target/killtarget/target2–4.
   Fire original SUB_UseTargets only after actual inventory acceptance, a
   surviving trigger and unchanged targets. Clear surviving targets after
   successful dispatch as reference; never create engine-owned target effects.
2. Eligible ammo/progression pickup that QC makes nontrigger can use the mod's
   existing zero-argument executable SUB_regen. Preserve **any** positive
   nextthink with a non-null/non-SUB_Null callback, even if overdue. Restore
   original touch/use only if current callback is null/SUB_Null; hide model
   and assign native think/nextthink. Finite delay clamps to>=1; refuse invalid
   delay/time or an unrepresentable float deadline before item mutation.
3. Preserve current retained-list lifetime and sharing/key transaction order.
   Isolate target activator, self/other/time, argc/parm/return scratch on normal
   return; intentionally authored gameplay globals/mutations remain. Stop
   follow-up work on original owner cancellation or item deletion. An invalid
   optional callback must not erase targets or install an invalid item think.
4. Copy all nine inherited controls/defaults/flags at existing host/server
   registration/header owners. Weapon level uses current finite tri-state
   helper and bounded >=2 selection, not primary's unsafe large-float int cast.
   Retain inherited class matching and optional target log; no new class cases.

## Design comparison and review decisions

| Option / current lean | Reusable owners, incompatibility and complexity |
| --- | --- |
| Copy primary helpers into world.c with native typed/retained boundaries | Adopt. Existing capture/acceptance, list, QC target/think and cvar owners remain. Target snapshot/log and native scheduler are bounded new policy that is actually missing. |
| Copy primary whole touch iteration or build a target/respawn engine | Reject. Replaces vkQuake retain/free behavior or duplicates mature mod callbacks without evidence. |
| Hand-roll another client/socket lifetime token for target/selector callbacks | Reject. Same-address bot replacement defeats socket identity; native sticky cancellation already exists. |
| Narrow target callback wrapper at current sv_phys policy owner | Lean adopt if required after review: bind/invoke/unbind existing callback scope; adapt the private invocation's explicit QC self/other and optional returned raw value, with ordinary calls retaining current defaults. Keep field capture, target clearing and scheduling in world.c. No public general callback framework or policy type. |
| Rely only on caller's existing physics scope | Considered smaller, but TouchLinks can also run through native QC linking without a current player respawn scope. Review actual paths before assuming it is sufficient. |

Open decisions for requested local Astra source advice: smallest selector/target
reuse signature, whether callback retirement requires a target-specific wrapper,
and any callback/order or allocation/policy duplication we can delete. The
separate COOP-008 selector needs the same owner/output adaptation; merge that
internal reuse only if simpler than parallel wrappers, not a broad callback API.
No need to re-review renderer/network/voice/avatars or redesign existing respawn.
Requested model/effort metadata are unobservable, so this is source advice and
does not claim formal senior-review certification.

Exact anticipated write set: `Quake/world.c`, `Quake/host.c`, `Quake/server.h`,
and narrowly `Quake/sv_phys.c` callback scope/private invocation plus target
wrapper. Target<=450 net production lines. Selector caller change in host_cmd
requires its own adopted disposition before coding. Reopen before adding state,
another policy owner or exceeding estimate. Main owns docs/metadata; one web
worker owns production regions after the review disposition is committed.

Final Linux/ARM proof covers modern/classic/explicit controls, ordinary stock
and custom touches, unchanged/changed/consumed/deleted/delayed targets, exact
once firing, rejected/no-op pickup, ammo-only acceptance without team ammo copy,
progression repeats, existing overdue think/null callbacks, invalid declarations,
native QC regeneration, callback retirement/recreation and nested target calls.
Use original assets read-only/disposable profiles. Native weapon/key sharing,
desktop/private/public movement and VR crossplay remain required.


## Adopted requested-Astra source disposition before coding

Main verified force-retouch before client physics, explicit QC touchtriggers,
actual drop/connect/free cancellation and the private invocation restoration.
The review changes the boundary; effective model settings remain unobservable.

| Recommendation | Main disposition |
| --- | --- |
| Protect original touch before reading accepted inventory, not only SUB_UseTargets | Adopt lifetime-only SV_CoopPickupTouch wrapper at the existing private cancellation owner. Bind original client/edict, execute the original PR_ExecuteProgram directly with native touch semantics, check sticky survival, unbind. World retains normal non-client dispatch. On cancellation break before teleport/shared/target/scheduler work; retained-list cleanup still runs. No isolated input/basis/trace restoration for the original native touch. |
| Reuse one invocation for target and selector output | Adopt private explicit QC self/other plus optional raw return-cell output, captured before ordinary restoration. Existing respawn calls retain client/world and no output. Narrow public wrappers only: SV_CoopPickupUseTargets(pickup,player,called) and SV_CoopSelectSpawnPoint(player,spawnprog). Policy type stays private; no generic public callback API. |
| Avoid redundant target and inventory plumbing | Adopt typed five-field snapshot for presence/comparison/clear. Existing before/after inventory captures serve sharing **or** target acceptance; capture pickup declarations only for sharing. Pickup deletion must not discard already accepted sharing, but prevents target/scheduler effects. |
| Preserve original item lifecycle and controls | Adopt all nine exact controls/defaults/flags, including five notify callbacks; no new classes. Pending positive think includes overdue callbacks. Native scheduler does not require captured inventory gain because equipment can change timers outside that snapshot. |
| Target function can rewrite fields during dispatch | Adapt primary clearing: clear only surviving typed fields still equal to the dispatched snapshot. Preserve callback-authored replacements. This bounded improvement needs no registry; target strings unchanged before touch alone do not prove every optional custom handler never fired them. Native/custom exact-once behavior stays an explicit final acceptance risk. |
| Original selector can recreate its caller at same addresses | Adopt existing bind/invoke/unbind wrapper and zero-argument positive in-range body gate. Existing host selection validates encoded return/fallback, but now takes captured player and reports survival separately; retirement prohibits fallback/diagnostics/relocation. This closes the two verified inherited COOP-008 source defects without new generations. |

Authorized write set adds only `Quake/host_cmd.c`'s selector helper and caller;
five existing production files, target<=450 net lines. Review estimate400–440
includes original selector scratch deletion. Do not compress checks to fit;
reopen if exceeded. Cancellation begins before original touch at every eligible
native/QC-linking entry. Per-dispatch scopes reuse existing sticky owner; no
scope token crosses files and no new persistent lifecycle state is added.
The native touch path preserves authored QC scratch, while **supplemental**
target/selector calls use existing isolation. One worker owns all five precise
regions; main reviews both reference policy and complete integrated patch.
