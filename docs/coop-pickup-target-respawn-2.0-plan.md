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
