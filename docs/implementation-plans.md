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
q30 prediction and complete native transitions remain unimplemented.
