# Major-feature implementation plans

Current [user scope decisions](migration-scope-decisions.md) supersede historical
requirements: Gorilla locomotion and instant stop are excluded from the goal.
Ordinary VR movement, swimming, ladders and predictive mod support remain required.

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
   Headset/eye-tracking trials, Windows qualification and performance
   measurement remain the user's deferred checkpoints. Linux/ARM software
   qualification is performed after full implementation, not before
   finishing the current implementation pass.
6. Open design decisions, chosen tradeoffs and an Astra senior-review disposition
   when the architecture has expensive or subtle forks. Reopen the plan if the
   work grows into a parallel owner or exceeds the stated scope.

The main agent integrates and reviews changes. Coding delegation uses the
user-requested model when available and explicit, nonoverlapping file ownership.
Unavailable model routes must not be silently replaced. Plans and implementation
commits stay on `2.0`; the product branch and unrelated user edits stay intact.

Features use shared engine behavior by default. Do not add a per-mod feature,
program whitelist, or ability implementation merely to qualify another mod.
Preserve inherited VR mod adapters and the demonstrated QBJ3 ladder fix;
additional exceptions require evidence of an actual incompatible boundary.

| Feature | Plan | Current planning state |
| --- | --- | --- |
| Production predictive movement and mod support | [Predictive movement plan](predictive-movement-2.0-plan.md), [stock state-transition slice](predictive-stock-transitions-2.0-plan.md), [arrival-gap slice](predictive-arrival-gap-2.0-plan.md), [startup resume slice](predictive-startup-resume-2.0-plan.md), [stock liquid slice](predictive-stock-liquid-2.0-plan.md), [pause ordering slice](predictive-pause-ordering-2.0-plan.md), [moving-brush slice](predictive-stock-pushers-2.0-plan.md), [ordinary stock activation](predictive-stock-activation-2.0-plan.md), [AD-family admission](predictive-mod-admission-2.0-plan.md) | Stock mode/recovery, wet replay, causal pause ordering and bounded moving-brush support pass local checks and Astra review. Ordinary stock default activation, native intermission/finale and delayed-contact correction pass consolidated checks and final Astra review. Plans preceded production changes and were reopened for demonstrated lifecycle/publication findings. AD architecture has an Astra disposition and intended state/phase table; ordinary replay and transition proof must accompany admission. Cooperative-QC admission and wider compatibility remain open. |
| Cooperative QuakeC movement | [Cooperative movement plan](predictive-cooperative-qc-2.0-plan.md), [accepted-command integration](predictive-cooperative-commands-2.0-plan.md), [VR identity adapter](predictive-cooperative-vr-input-2.0-plan.md) | Standard server builtin347/native hook and accepted-command integration pass prepared loader/VM/body/send checks, relevant regressions and final local Astra source review. Plans/dispositions preceded edits; quiet-hook and sticky-handoff contracts are explicit. VR identity now preserves bounded swim/ladder consumer behavior with exact target-call/nested isolation and decoded-body proof. Replay and broader states remain required. Gorilla and instant stop are excluded. |
| OpenXR lifecycle and stereo renderer | [Migration architecture plan](vkquake-base-migration-plan.md), [stereo review](migration-stereo-review.md), [frame ownership review](migration-frame-boundary-review.md), [device reconstruction plan](openxr-device-reconstruction-2.0-plan.md) | Compatible late binding is implemented. Incompatible-device reconstruction has a local Astra owner/reuse disposition; texture retirement, [alias replay](openxr-alias-replay-2.0-plan.md) and [brush vertex regeneration](openxr-brush-vertex-replay-2.0-plan.md) pass final local Astra source review. The [lightmap reconstruction plan](openxr-lightmap-replay-2.0-plan.md) has its existing-record upload adapter source-integrated with final local Astra acceptance; CPU regeneration and image replay/cache activation are also source-integrated with final local Astra acceptance; dependent GPU owners are the next design slice. Full lightmap/resource retirement/reconstruction remains open. Ordinary `vid_restart` does not recreate the device. |
| VR input and locomotion | [Input review](migration-input-review.md), [locomotion review](migration-locomotion-review.md), [Gorilla review](migration-gorilla-2.0-review.md) | Existing designs/reviews; plan any new input or movement behavior before implementation. |
| Vulkan foveation | [FB/META preference and KHR fallback plan](openxr-foveation-selection-2.0-plan.md), [quad-view assessment](openxr-quad-views-2.0-assessment.md), [historical foveation plan](vulkan-foveation-2.0-plan.md), [Steam Frame review](migration-steam-frame-foveation-review.md), [late device-readiness review](openxr-late-foveation-2.0-plan.md) | Ordinary startup retains KHR where capable; unqualified FB/META requires the explicit development switch. Default-MSAA support is source-adapted and the borrowed-image contract remains a release blocker. Quad views are excluded from 2.0. The existing non-FB attachment safeguard remains useful. End-of-goal verification is pending. |
| VR SSAO | [Existing SSAO design and review](migration-vr-ssao-review.md) | Existing design/review; revise before another major algorithm change. |
| 3D weapon wheel | [Existing wheel design](migration-wheel-3d-design.md), [foreground review](migration-wheel-foreground-review.md), [current-primary delta plan](migration-wheel-primary-delta-2.0-plan.md) | Stages1..4 source-integrated with final local Astra acceptance: stock/native held identities, authored partial overlays, complete rosters, parent upgrades, variant cache and vr_weaponlist. Current catalog and Vulkan presentation owners remain. End-of-goal qualification is pending. |
| VR HUD and multiplayer presentation | [HUD design](vr-multiplayer-hud-design.md), [desktop parity review](migration-desktop-parity-review.md) | Existing design/review; new UI behavior needs a feature plan and desktop/VR acceptance. |
| Weapon calibration and mod discovery | [Adjustment implementation review](migration-weapon-adjustment-implementation-review.md), [runtime discovery review](migration-weapon-runtime-discovery-review.md) | Existing reviews; future expansion must plan shared offsets and actual mod identity/discovery behavior. |
| Physical attacks and akimbo | [Akimbo integration review](migration-akimbo-integration-review.md), [server review](migration-akimbo-server-review.md) | Existing reviews; plan new combat behavior against the inherited physical/native attack owners. |
| Remaining inherited physical-contact melee | [Historical Copper reuse plan](migration-copper-melee-2.0-plan.md), [current scope](migration-scope-decisions.md) | Deferred by the user in favor of gesture-only activation. Uncommitted Copper server changes removed. Retain existing server/wire owners; no new exact attack adapters or parry/reach solver required in this pass. |
| Universal melee gestures | [Generic gesture foundation](migration-generic-melee-2.0-plan.md), [gesture-only plan](migration-gesture-only-melee-2.0-plan.md), [configuration](generic-melee.md) | Gesture-only native attack, selective physical-attack suppression, paired-hand recognition and steady ready-pose presentation implemented with shared profiles. Final local Astra Max source review accepted, including explicit primary-branch reference comparison and offhand reflection correction. Linux/ARM command/QC/render qualification at the end; physical contact damage remains deferred. |
| Linux native audio/build packaging | [Linux packaging plan](linux-native-packaging-2.0-plan.md) | Reuse inherited native Steam Audio recipe and pinned dependencies; adapt game package to vkQuake Meson. Linux/ARM qualification at the end. |
| Vulkan avatars/equipment | [Existing avatar design](avatar-vulkan-review.md), [equipment review](avatar-equipment-vulkan-review.md) | Existing design/review; revise before expanding rig or geometry ownership. |
| Expanded co-op | [Inventory review](migration-coop-inventory-review.md), [outline design](migration-coop-outline-design.md), [friendly-fire review](migration-friendly-fire-review.md) | Existing designs/reviews; new co-op behavior requires a bounded plan across public desktop and private VR peers. |
| Spatial audio and voice capture | [Spatial audio review](migration-spatial-audio-review.md), [system-default microphone plan](voice-default-device-2.0-plan.md) | Existing mixer owners remain. The user's system-default microphone and VR default-on/saved-opt-out policy reuse SDL capture and settings; desktop/VR profile switching is part of the bounded voice slice. |
| Large maps and renderer performance | [Renderer feature map](migration-renderer-map.md), [alias instancing review](migration-alias-instancing-review.md) | Existing inventory/review; select a concrete optimization and write its reuse, correctness and acceptance plan before implementation. Performance measurement stays deferred. |

The [OpenXR session recovery plan](openxr-session-recovery-2.0-plan.md) adds
explicit re-enable after a session-only loss or EXITING using the original
runtime-created Vulkan binding. Every new attachment requalifies system/API/GPU;
failed session destruction and instance loss abandon backend eligibility. Linux
build and bounded backend/renderer/camera/input-helper checks pass. Final Astra
Max source review accepted the polling correction with no remaining blocker in
this slice. The subsequent [late-binding plan](openxr-late-binding-2.0-plan.md)
adds compatible-device desktop attachment and fresh instance recovery using
actual creation metadata and original-Vulkan qualification. Its Linux component
checks pass; a later native-GPU run also passes the actual donor six-set layout
owners, while loaded-scene XR/GPU proof remains open. Incompatible-device reconstruction remains parent implementation scope. The
[late-foveation plan](openxr-late-foveation-2.0-plan.md) records the device-time
readiness adapter and its remaining proof limits. At the user's request, no
further builds or tests will run until implementation of the full goal is done;
live checks remain user-deferred.

These links do not certify that a feature is finished. Completed work predating
this index retains its original design/review provenance; this document does
not retrospectively claim that every earlier change had a written preimplementation
plan. The [complete feature map](migration-feature-map.md) still defines the
full migration scope. Optional candidates stay proposals until selected, and
skyrooms remain outside the goal.

The next [general initial-mod-state admission plan](predictive-initial-mod-state-2.0-plan.md)
precedes changes to nonstock initial wet/native/custom admission. It reuses the
existing pre-begin classifier and observational frame validator. Initial wet/
FLY/NOCLIP/custom-hull/customphysics and malformed-state refusal, first native
publication and older AD dry return pass Linux checks and final local Astra
source review. Plans/reopening preceded the production boundary changes; complete
local/load/state/replay compatibility remains parent scope.

The [local and restored private movement plan](predictive-local-load-2.0-plan.md)
reuses the same negotiation, command, physics, completion, snapshot and loaded
identity owners for single-slot clients. Its Astra disposition adds private
fastload reconnect isolation and stock stale-ground refusal before selection.
The implementation and Linux checks cover actual loopback movement/fire/replay,
host menu/pause recovery, native/public fallback, v5 private load entry routes,
public fastload preservation and v7 first-player movement while a second saved
identity remains pending. Prepared renderer resources and a synthetic second
endpoint retain explicit evidence limits; this does not certify the whole
save, graphical/OpenXR lifecycle or full migration inventory.

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

Local Astra Max resolved the shared-movement reassessment in favor of the
inherited world-QC/command-solver organization adapted at existing2.0 owners,
with actual force ownership and general admission. The committed disposition
and executable-prefix/phase contract define the different-program AD pak0
vertical. Its bounded shared-QC adapter is implemented: Linux build, actual
boots lifecycle, queued weapon/pose/roomscale input, retained native suffix and
callback-composition checks pass. Final local Astra Max source review has no
remaining blocker in this slice. The linked checkpoint retains the failed
native numerical comparison and exact prepared/captured proof limits. Broader
wet/cooperative/local/load compatibility remains in the full goal. No new
production boots code was needed for the actual AD/q30 lifecycle proof.
