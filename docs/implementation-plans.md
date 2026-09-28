# Major-feature implementation plans

Before a new major feature is implemented on `2.0`, record its plan here and in
a linked feature document. A feature inventory or a retrospective review alone
is not an implementation plan. Plans should remain small enough to guide the
next end-to-end change; they must retain the complete feature's intended outcome.

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
| Production predictive movement and mod support | [Predictive movement plan](predictive-movement-2.0-plan.md), [stock state-transition slice](predictive-stock-transitions-2.0-plan.md), [arrival-gap slice](predictive-arrival-gap-2.0-plan.md), [startup resume slice](predictive-startup-resume-2.0-plan.md), [stock liquid slice](predictive-stock-liquid-2.0-plan.md), [pause ordering slice](predictive-pause-ordering-2.0-plan.md) | Stock noclip/fly, arrival-gap, startup-resume, swim handoff and callback/timer corrections implemented. Experimental live wet replay, bounded ledge oracle and causal pause ordering pass local checks; final Astra review found no remaining source blockers in this slice. Full-domain qualification, selected activation and mod admission remain later stages. |
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
