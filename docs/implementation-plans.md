# Major-feature implementation plans

Before a new major feature is implemented on `2.0`, record its plan here and in
a linked feature document. A feature inventory or a retrospective review alone
is not an implementation plan. Plans should remain small enough to guide the
next end-to-end change; they must retain the complete feature's intended outcome.
Commit the plan before its production implementation. Revise and commit it
before expanding a major feature's scope or changing its architecture. Routine
fixes within an existing contract can use that plan; a new behavior, protocol,
renderer algorithm or movement owner needs an explicit updated contract first.

Each plan must contain:

1. User-visible behavior and the behavioral reference, including desktop/VR
   boundaries and intentional improvements.
2. Verified current-state evidence and explicit unknowns.
3. A comparison of the smallest adapter with replacement, identifying reusable
   owners, demonstrated incompatibilities, duplicated state and expected scope.
4. Concrete implementation stages, files/owners and dependencies. Define the
   smallest usable vertical slice and how later stages reach the full outcome.
5. Relevant software acceptance checks, negative cases and completion evidence.
   Headset/eye-tracking trials, Windows/ARM qualification and performance
   measurement remain the user's deferred checkpoints, not prerequisites to
   finishing the current implementation pass.
6. Open design decisions, chosen tradeoffs and an Astra senior-review disposition
   when the architecture has expensive or subtle forks. Reopen the plan if the
   work grows into a parallel owner or exceeds the stated scope.

The main agent integrates and reviews changes. Coding delegation uses the
user-requested model when available and explicit, nonoverlapping file ownership.
Unavailable model routes must not be silently replaced. Plans and implementation
commits stay on `2.0`; the product branch and unrelated user edits stay intact.

| Feature | Plan | Current planning state |
| --- | --- | --- |
| Production predictive movement and mod support | [Predictive movement plan](predictive-movement-2.0-plan.md), [stock state-transition slice](predictive-stock-transitions-2.0-plan.md), [arrival-gap slice](predictive-arrival-gap-2.0-plan.md), [startup resume slice](predictive-startup-resume-2.0-plan.md), [stock liquid slice](predictive-stock-liquid-2.0-plan.md), [pause ordering slice](predictive-pause-ordering-2.0-plan.md), [moving-brush slice](predictive-stock-pushers-2.0-plan.md), [ordinary stock activation](predictive-stock-activation-2.0-plan.md), [AD-family admission](predictive-mod-admission-2.0-plan.md) | Stock mode/recovery, wet replay, causal pause ordering and bounded moving-brush support pass local checks and Astra review. Ordinary stock default activation, native intermission/finale and delayed-contact correction pass consolidated checks and final Astra review. Plans preceded production changes and were reopened for demonstrated lifecycle/publication findings. AD architecture has an Astra disposition and intended state/phase table; ordinary replay and transition proof must accompany admission. Cooperative-QC admission and wider compatibility remain open. |
| OpenXR lifecycle and stereo renderer | [Migration architecture plan](vkquake-base-migration-plan.md), [stereo review](migration-stereo-review.md), [frame ownership review](migration-frame-boundary-review.md) | Existing architecture/reviews; write a bounded feature plan before expanding runtime or renderer ownership. |
| VR input and locomotion | [Input review](migration-input-review.md), [locomotion review](migration-locomotion-review.md), [Gorilla review](migration-gorilla-2.0-review.md) | Existing designs/reviews; plan any new input or movement behavior before implementation. |
| Vulkan foveation | [Existing foveation plan](vulkan-foveation-2.0-plan.md), [Steam Frame review](migration-steam-frame-foveation-review.md) | Existing plan/review; revise before another major capability or architecture expansion. |
| VR SSAO | [Existing SSAO design and review](migration-vr-ssao-review.md) | Existing design/review; revise before another major algorithm change. |
| 3D weapon wheel | [Existing wheel design](migration-wheel-3d-design.md), [foreground review](migration-wheel-foreground-review.md) | Existing design/review; revise before expanding presentation ownership. |
| VR HUD and multiplayer presentation | [HUD design](vr-multiplayer-hud-design.md), [desktop parity review](migration-desktop-parity-review.md) | Existing design/review; new UI behavior needs a feature plan and desktop/VR acceptance. |
| Weapon calibration and mod discovery | [Adjustment implementation review](migration-weapon-adjustment-implementation-review.md), [runtime discovery review](migration-weapon-runtime-discovery-review.md) | Existing reviews; future expansion must plan shared offsets and actual mod identity/discovery behavior. |
| Physical attacks and akimbo | [Akimbo integration review](migration-akimbo-integration-review.md), [server review](migration-akimbo-server-review.md) | Existing reviews; plan new combat behavior against the inherited physical/native attack owners. |
| Vulkan avatars/equipment | [Existing avatar design](avatar-vulkan-review.md), [equipment review](avatar-equipment-vulkan-review.md) | Existing design/review; revise before expanding rig or geometry ownership. |
| Expanded co-op | [Inventory review](migration-coop-inventory-review.md), [outline design](migration-coop-outline-design.md), [friendly-fire review](migration-friendly-fire-review.md) | Existing designs/reviews; new co-op behavior requires a bounded plan across public desktop and private VR peers. |
| Spatial audio | [Spatial audio review](migration-spatial-audio-review.md) | Existing design/review; new mixer/provider ownership requires a plan preserving desktop behavior. |
| Large maps and renderer performance | [Renderer feature map](migration-renderer-map.md), [alias instancing review](migration-alias-instancing-review.md) | Existing inventory/review; select a concrete optimization and write its reuse, correctness and acceptance plan before implementation. Performance measurement stays deferred. |

These links do not certify that a feature is finished. Completed work predating
this index retains its original design/review provenance; this document does
not retrospectively claim that every earlier change had a written preimplementation
plan. The [complete feature map](migration-feature-map.md) still defines the
full migration scope. Optional candidates stay proposals until selected, and
skyrooms remain outside the goal.

The [stock activation plan](predictive-stock-activation-2.0-plan.md) was committed
before changing defaults, then reopened before the source-proven intermission
fix. Its implementation retains existing movement/QC/completion owners.
The next [AD-family admission plan](predictive-mod-admission-2.0-plan.md) records
the complete contract and design questions before new production edits;
its Astra disposition, intended state/phase table and exact next-slice ownership
are now recorded. Initial wet/load/single-slot cases remain native;
broader mod and local/load compatibility must not disappear from the full goal.

The first exact-q30 dry/callback comparison is committed as `489dfe5e`.
The next [ordinary q30 admission/replay plan](predictive-q30-replay-2.0-plan.md)
records the missing replay consumer, bounded compatibility and velocity-limit
inputs, pre-QC VR ordering, exact write set and end-to-end acceptance before
production edits. Its Astra review changed the ordering contract; ordinary
q30 policy transport, complete movement inputs and the actual replay consumer
are implemented and pass local component checks. At that checkpoint ordinary
q30 admission and complete native transitions remained incomplete. Later slices
below qualify bounded closure/traversal and actual normal-session activation;
real ability/trigger traversal and wider compatibility remain required.

The next [q30 native-state and admission plan](predictive-q30-transitions-2.0-plan.md)
records the actual state inventory, fresh-native reuse, bounded write set and
normal-session acceptance before production edits. Typed classification and
bounded native dispatch are implemented and locally reviewed by Astra; the
[checkpoint review](predictive-q30-transition-implementation-review.md) records
software evidence and limits, including a reproduced preexisting stock-liquid
assertion. Ordinary PreThink closure is now verified by a separate local Astra
audit; the [scheduled damage-target contract](predictive-q30-callback-closure-review.md)
records the demonstrated HP-target camera gap and its pre-staging native dispatch.
Remaining scheduled callbacks, actual traversal and normal admission remain open.
The same record planned the synchronous shotgun/lightning target extension
before implementation and records a separate projectile closure audit. Successful
dry launches and ordinary animations have bounded closure evidence; empty-ammo
forced weapon selection has a bounded native handoff with local Astra review.
An actual native-QC last-nail sequence demonstrates that gap; the
[empty-ammo handoff plan](predictive-q30-empty-ammo-2.0-plan.md) defines its
conditional existing-boundary fix and selected/native proof before production.
The implemented gate and all20 roots plus ordinary fallback pass Linux component
checks. Actual traversal and complete normal admission still remain open.
The [real-BSP traversal plan](predictive-q30-traversal-2.0-plan.md) was committed
before shared-finder/actual-sweep/retained-head edits. Its prepared airborne
crossing on shipped1024_tango, actual dry prefix and next native consumption
pass Linux checks and final local Astra review. A separate local Astra audit
closes normally initialized client/target scheduling without another gate;
arbitrary save/command mutations and full normal admission remain outside that
bounded closure. Production is unchanged by this traversal slice.
Revise the plan before implementing any newly expanded phase contract.

The next [normal q30 activation plan](predictive-q30-activation-2.0-plan.md)
records program/policy admission, qualified replay permission and the actual
offer/spawn/begin/command/public-peer/full-parser/replay vertical before edits.
It was reopened before fixes to the mutating water publication check and weak
capture/replay assertions. Bounded normal-session activation, numerical replay
versus authoritative completion, horizontal public movement and observational
publication/next-native parity now pass consolidated Linux checks and final local
Astra Max review. Component selection or permission alone cannot qualify the
session. Real ability/trigger and wider AD/Mjolnir/cooperative-QC requirements
remain open, as does the separately reproduced preexisting stock10ms liquid
assertion. The plan records the fixtures' prepared/captured proof limits.

The [q30 jump-boots lifecycle plan](predictive-q30-boots-2.0-plan.md) precedes
actual pickup/native-action/expiry/replay-return fixture implementation. It reuses
installed QC spawn/contact and existing normal-session owners, with separate
initialized native/selected runs and explicit prepared item/resource limits.
No new production ability or movement owner is authorized by this slice.

The [installed AD identity/reuse plan](predictive-ad-identical-program-2.0-plan.md)
records byte-identical pak2 program evidence before actual mounted-session
qualification. It uses existing exact-program owners without a new production
implementation; older AD programs and Mjolnir remain separately qualified.

The user's direction to avoid mod-special-case proliferation reopens the
[movement reuse decision](predictive-movement-reuse-reassessment-2.0-plan.md).
It compares verified QSS-M independent/native defaults and the inherited shared
QC wrapper against the current exact-program path. A local Astra disposition
and bounded shared-owner contract must precede further production expansion.
The deliberate QBJ3 ladder fix and all inherited VR behavior remain required.
