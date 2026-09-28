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
| Production predictive movement and mod support | [Predictive movement plan](predictive-movement-2.0-plan.md), [stock state-transition slice](predictive-stock-transitions-2.0-plan.md) | Demo compatibility and ordinary private transport activation implemented. Stock state-transition plan drafted before edits; selected movement activation and mod admission remain later stages. |
| Vulkan foveation | [Existing foveation plan](vulkan-foveation-2.0-plan.md), [Steam Frame review](migration-steam-frame-foveation-review.md) | Existing plan/review; revise before another major capability or architecture expansion. |
| VR SSAO | [Existing SSAO design and review](migration-vr-ssao-review.md) | Existing design/review; revise before another major algorithm change. |
| 3D weapon wheel | [Existing wheel design](migration-wheel-3d-design.md), [foreground review](migration-wheel-foreground-review.md) | Existing design/review; revise before expanding presentation ownership. |
| Vulkan avatars/equipment | [Existing avatar design](avatar-vulkan-review.md), [equipment review](avatar-equipment-vulkan-review.md) | Existing design/review; revise before expanding rig or geometry ownership. |

These links do not certify that a feature is finished. Completed work predating
this index retains its original design/review provenance; this document does
not retrospectively claim that every earlier change had a written preimplementation
plan. The [complete feature map](migration-feature-map.md) still defines the
full migration scope. Optional candidates stay proposals until selected, and
skyrooms remain outside the goal.
