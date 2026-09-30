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
| Inherited QuakeC builtin compatibility | [Registry compatibility plan](qc-builtin-compatibility-2.0-plan.md), [occupied-slot design and disposition](qc-builtin-slot-2.0-plan.md), [file-search adapter](qc-file-search-2.0-plan.md), [capability-query adapter](qc-capability-query-2.0-plan.md), [client weather adapter](qc-weather-2.0-plan.md), [line-drawing adapter](qc-drawline-2.0-plan.md), [client/server events](qc-events-2.0-plan.md), [core-name discovery](qc-core-discovery-2.0-plan.md), [player colors](qc-player-colors-2.0-plan.md), [named calls](qc-named-calls-2.0-plan.md), [resource ownership](qc-resource-ownership-2.0-plan.md), [small service contracts](qc-service-contracts-2.0-plan.md), [inline-surface bounds](qc-inline-surface-2.0-plan.md), [field reflection](qc-reflection-2.0-plan.md), [debug fallback decision](qc-debug-fallback-2.0-plan.md) | Discovery, cvar/HUD services, buffer-file/search operations, alternate strconv slot, identified colliding-slot adapters, bounded capability queries, CSQC weather/line drawing and the paired client/server event adapter have local Astra source acceptance. Native VM/filesystem/platform/registry/loader/rendering owners remain. Queries retain native predicates and overrides with primary case/disable behavior and three verified aliases; skipped startup-disabled SSQC setup still requires program reload. Line drawing shares native solid GUI submission with unchanged drawfill behavior. Core-name discovery and named core declarations reuse native dispatch and have bounded local Astra source acceptance. Player colors reuse native userinfo with canonical palette values, preflight/name seeding, team restoration and the copied QSS-M prefix fix; named calls share native lazy dispatch for builtin targets. Both have local Astra source acceptance. File/buffer ownership now enforces the current VM through native tables, with copied sparse-sort tail clearing and bounded Astra source acceptance. Primary registercvar/default/status and buffer-add/cvarlist wrapper corrections also have bounded Astra source acceptance. Inline point/triangle bounds, clipped-point refusal/projection and invalid normal returns have bounded local Astra source acceptance; native model/cache owners remain and software qualification is pending. Field reflection has bounded local Astra source acceptance for map rebinding, type masking, server relinking and zoned alias lifetime. The unsupported-debug collision adapter has bounded Astra source acceptance after its advisory design disposition; identified inherited declarations error through the existing loader while native numeric meanings remain. Remaining interface/error/permission and capability contracts remain open; final software checks are deferred. |
| Production predictive movement and mod support | [Predictive movement plan](predictive-movement-2.0-plan.md), [stock state-transition slice](predictive-stock-transitions-2.0-plan.md), [arrival-gap slice](predictive-arrival-gap-2.0-plan.md), [startup resume slice](predictive-startup-resume-2.0-plan.md), [stock liquid slice](predictive-stock-liquid-2.0-plan.md), [pause ordering slice](predictive-pause-ordering-2.0-plan.md), [moving-brush slice](predictive-stock-pushers-2.0-plan.md), [ordinary stock activation](predictive-stock-activation-2.0-plan.md), [AD-family admission](predictive-mod-admission-2.0-plan.md) | Stock mode/recovery, wet replay, causal pause ordering and bounded moving-brush support pass local checks and Astra review. Ordinary stock default activation, native intermission/finale and delayed-contact correction pass consolidated checks and final Astra review. Plans preceded production changes and were reopened for demonstrated lifecycle/publication findings. AD architecture has an Astra disposition and intended state/phase table; ordinary replay and transition proof must accompany admission. Cooperative-QC admission and wider compatibility remain open. |
| Cooperative QuakeC movement | [Cooperative movement plan](predictive-cooperative-qc-2.0-plan.md), [accepted-command integration](predictive-cooperative-commands-2.0-plan.md), [VR identity adapter](predictive-cooperative-vr-input-2.0-plan.md) | Standard server builtin347/native hook and accepted-command integration pass prepared loader/VM/body/send checks, relevant regressions and final local Astra source review. Plans/dispositions preceded edits; quiet-hook and sticky-handoff contracts are explicit. VR identity now preserves bounded swim/ladder consumer behavior with exact target-call/nested isolation and decoded-body proof. Replay and broader states remain required. Gorilla and instant stop are excluded. |
| OpenXR lifecycle and stereo renderer | [Migration architecture plan](vkquake-base-migration-plan.md), [stereo review](migration-stereo-review.md), [frame ownership review](migration-frame-boundary-review.md), [focused Vulkan scope disposition](openxr-device-reconstruction-2.0-plan.md#focused-astra-max-scope-disposition) | Preserve explicit OpenXR startup, compatible desktop late attachment, actual creation metadata and existing same-device session/mode transitions. General live incompatible-device replacement/device-loss recovery is deferred; existing refusal explains process restart with `-openxr`. Unused alias retention and lightmap replay are removed with final local Astra source acceptance; native upload/update paths remain. Preserve native graphics, useful initial-source validation and the reduced CPU-polygon design. End-of-implementation Linux/ARM and desktop/stereo qualification remain open. |
| VR input and locomotion | [Input review](migration-input-review.md), [locomotion review](migration-locomotion-review.md), [Gorilla review](migration-gorilla-2.0-review.md) | Existing designs/reviews; plan any new input or movement behavior before implementation. |
| Vulkan foveation | [FB/META preference and KHR fallback plan](openxr-foveation-selection-2.0-plan.md), [two-eye density offset plan and adopted advisory](openxr-density-offsets-2.0-plan.md), [static-profile update reuse](openxr-static-profile-2.0-plan.md), [quad-view assessment](openxr-quad-views-2.0-assessment.md), [historical foveation plan](vulkan-foveation-2.0-plan.md), [Steam Frame review](migration-steam-frame-foveation-review.md), [late device-readiness review](openxr-late-foveation-2.0-plan.md) | Reopened offset placement from official Meta/Qualcomm guidance and pinned Godot source; bounded native attachment/sample/META flag correction implemented92b68e9d, startup-mode readiness correction6a519ade closed by requested-Astra source advisory. Native sample policy and existing XR/pass owners remain. Ordinary startup retains KHR where capable; unqualified FB/META requires the temporary development switch. Automatic qualified FB/META preference remains required. The borrowed-image contract remains a release blocker. Quad views are excluded from 2.0. End-of-goal verification is pending. |
| VR SSAO | [Existing SSAO design and review](migration-vr-ssao-review.md) | Existing design/review; revise before another major algorithm change. |
| 3D weapon wheel | [Existing wheel design](migration-wheel-3d-design.md), [foreground review](migration-wheel-foreground-review.md), [current-primary delta plan](migration-wheel-primary-delta-2.0-plan.md) | Stages1..4 source-integrated with final local Astra acceptance: stock/native held identities, authored partial overlays, complete rosters, parent upgrades, variant cache and vr_weaponlist. Current catalog and Vulkan presentation owners remain. End-of-goal qualification is pending. |
| VR HUD and multiplayer presentation | [HUD design](vr-multiplayer-hud-design.md), [desktop parity review](migration-desktop-parity-review.md), [native CSQC HUD clock adapter](qc-hud-clock-2.0-plan.md) | The optional inherited intermission clock is source-integrated666cdab1 at the existing HUD callback, with requested-Astra source acceptance. Native picture alpha/subpicture coordinate paths remain reused. Broader desktop/VR qualification and new UI behavior require their own acceptance. |
| Weapon calibration and mod discovery | [Adjustment implementation review](migration-weapon-adjustment-implementation-review.md), [runtime discovery review](migration-weapon-runtime-discovery-review.md) | Existing reviews; future expansion must plan shared offsets and actual mod identity/discovery behavior. |
| Physical attacks and akimbo | [Akimbo integration review](migration-akimbo-integration-review.md), [server review](migration-akimbo-server-review.md) | Existing reviews; plan new combat behavior against the inherited physical/native attack owners. |
| Remaining inherited physical-contact melee | [Historical Copper reuse plan](migration-copper-melee-2.0-plan.md), [current scope](migration-scope-decisions.md) | Deferred by the user in favor of gesture-only activation. Uncommitted Copper server changes removed. Retain existing server/wire owners; no new exact attack adapters or parry/reach solver required in this pass. |
| Universal melee gestures | [Generic gesture foundation](migration-generic-melee-2.0-plan.md), [gesture-only plan](migration-gesture-only-melee-2.0-plan.md), [configuration](generic-melee.md) | Gesture-only native attack, selective physical-attack suppression, paired-hand recognition and steady ready-pose presentation implemented with shared profiles. Final local Astra Max source review accepted, including explicit primary-branch reference comparison and offhand reflection correction. Linux/ARM command/QC/render qualification at the end; physical contact damage remains deferred. |
| Campaign and mod localization | [Inherited message-source adapter](localization-message-sources-2.0-plan.md) | Verified missing generic FGD source and inherited fallback contracts; retain native language/allocator/hash/VM owners. Stage 1 awaits local Astra design disposition; separate inherited rerelease discovery remains required. Final Linux/ARM checks deferred. |
| Linux native audio/build packaging | [Linux packaging plan](linux-native-packaging-2.0-plan.md) | Reuse inherited native Steam Audio recipe and pinned dependencies; adapt game package to vkQuake Meson. Linux/ARM qualification at the end. |
| Vulkan avatars/equipment | [Existing avatar design](avatar-vulkan-review.md), [equipment review](avatar-equipment-vulkan-review.md) | Existing design/review; revise before expanding rig or geometry ownership. |
| Builtin avatar endpoint adapters | [Desktop repairs](avatar-desktop-repairs-2.0-plan.md), [tracked endpoints](avatar-tracked-endpoints-2.0-plan.md), [tracked animal Hip](avatar-tracked-hip-2.0-plan.md) | Reference math reused in existing CPU palette before attachment/bounds. Desktop support/Shambler, tracked head/wrists, supplied feet, final Vore mirrored poles and animal Hip position/confidence have bounded local Astra source acceptance. Native Vulkan owners remain. Numerical/asset/Linux/ARM qualification remains deferred. |
| QC string and buffer wrapper repairs | [String/buffer repair plan](qc-string-buffer-repairs-2.0-plan.md) | Source-proven cvar-list dangling storage, replacement capacity and missing second comparison offset repaired through native owners, with bounded local Astra source acceptance. Actual primary replacement helper copied; native lexical ordering retained. Full ABI/Linux/ARM qualification remains deferred. |
| QC file write and cached-read compatibility | [File-service plan and advisory disposition](qc-file-write-2.0-plan.md) | Primary append mode/parent helper copied in06cc04ea. Bounded local Astra audit found two retained native defects; slot initialization and consumed-cache position corrected in15e37a3c with main source acceptance. Native dynamic handles, VM/filesystem/cache owners and deliberate path/line policies remain. Final Linux/ARM checks deferred. |
| QC palette explosion arguments | [Existing wrapper correction plan](qc-explosion-palette-2.0-plan.md) | Two native wrappers consume their distinct third palette-length argument in9ebfb93c. Native vkQuake effect counts, packet, lights, sound and rendering remain. Main source acceptance only; execution deferred. |
| Native extended parser reuse | [Current-source NET-008 checkpoint](migration-network-map.md#current-source-parser-reuse-checkpoint-2026-09-30) | Existing absolute-light byte consumption, setangledelta plus VR angle adapter and public CSQC event dispatch retained. Historical inventory gaps do not justify parser replacement. Selected private PEXT1 stays zero; final software qualification deferred. |
| Remaining QC string/token audit | [Source audit and capacity disposition](qc-string-token-source-audit-2.0.md) | No additional confirmed scoped defect; retain native larger parser tokens and existing bounded temporary argv output. Broader interface/runtime qualification remains open. |
| QC client stats and picture precache | [Client wrapper repair plan](qc-client-stat-picture-2.0-plan.md) | Exclusive stat-bound and optional precache flags/failure-reporting repairs implemented from primary reference with bounded local Astra source acceptance. Native cache/VM/rendering owners remain. Final Linux/ARM qualification deferred. |
| Desktop QC clipping | [Vulkan boundary repair plan](qc-desktop-clip-2.0-plan.md) | Desktop-only scissor intersection adapter implemented with bounded local Astra source acceptance. Existing VR panel source clipping and native canvas/rendering owners remain. Final Linux/ARM qualification deferred. |
| Recipient visibility and attachment snapshots | [NET-003 incremental reuse plan](snapshot-visibility-2.0-plan.md) | Source adapter committed in72510659: inherited optional fields/callbacks, parent PVS, native owner, bounded references, classic sorting/retention and custom no-remove/replay. Connection-lifetime decision reopened and source-corrected in3633bf03 using native active/socket identity; bounded local Astra advisory accepted all three callback boundaries and four failure callers. No renderer/wire replacement. Final Linux/ARM/visible-behavior qualification deferred. |
| Native entity particle emission guard | [Reference guard plan](particle-emission-precache-2.0-plan.md) | Primary bounded/named precache guard reused at existing dynamic/static emission consumers in `9e1709c5`, preserving native effects and model fallback. Main source/whitespace review only; execution deferred. |
| CSQC custom entity transport | [NET-009 reuse plan and verified brief](csqc-entity-transport-2.0-plan.md) | Inherited stream source-implemented, reusing native frame ACK/VM owners and preserving mandatory owner/private headers. Stable retirement ACK boundary correction `39ea8209` accepted in bounded local Astra source advisory; reset-loop/prespawn corrections likewise reviewed. End-to-end loss/lifecycle qualification and final Linux/ARM checks deferred; no full feature completion claim. |
| QC entity copy and player queries | [Entity/query repair plan](qc-entity-copy-player-query-2.0-plan.md) | Payload/free, pre-allocation source and allocated-slot guards source-implemented with bounded local Astra advisory acceptance. Revised current-VM linking disposition preserves native full-game CSQC spatial updates. Native entity/VM/scoreboard/userinfo owners remain. Final Linux/ARM qualification deferred. |
| Client QC model lookup | [Existing callback repair plan](qc-client-model-lookup-2.0-plan.md) | Bounded QSS-M model helper installed in actual client initialization, retaining native precache and wrappers; bounded local Astra advisory source acceptance. Final Linux/ARM qualification deferred. |
| Builtin desktop avatar repairs | [Reference repair adapter](avatar-desktop-repairs-2.0-plan.md) | Inherited Shambler anatomical arms and Ogre/Shambler attached-prop support grip source-implemented at the existing palette boundary. Astra's near-antipodal transport finding corrected with desktop-only axis continuity and confirmed by final bounded source recheck. No additional rendering owner; inactive waist socket excluded by current-reference evidence. Full avatar parity and final software qualification are not certified. |
| VR alternate underwater effect | [Verified brief and Astra advisory disposition](vr-underwater-projection-2.0-plan.md) | Native alternate FOV warp source-adapted to two-view scene clips, culling and AO, with bounded advisory source review. Display/UI/masks/foveation retain ordinary coordinates. The reviewed whole-frame identity guard pauses alternate deformation during interactive VR wheel use, preserving its existing selection/depth/culling contract. Desktop and native Vulkan owners remain; final software qualification deferred. |
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
owners, while loaded-scene XR/GPU proof remains open. The focused
[Vulkan scope disposition](openxr-device-reconstruction-2.0-plan.md#focused-astra-max-scope-disposition)
defers general live incompatible-device replacement and device-loss recovery;
keep existing startup and compatible mode/session transitions. Unused alias
payload retention and lightmap replay paths are removed; these prerequisite helpers no longer
define the next required migration stage. The
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

The [stock wet-entry follow-up](predictive-stock-wet-entry-2.0-plan.md) removes
the leftover dry-only initial selection gate by reusing common begin validation.
Supported stock native starts retain their existing authority and can return
to WALK. Restored unowned ledge jumps finish under the existing native owner;
owned/provisional solver timers remain unchanged. Personal Astra Max accepted
the two-file source delta; end-of-implementation qualification is pending.

A current cooperative source audit found liquid execution, imported ladder
controls and native-to-command return present. Its concrete missing collision
boundary is addressed by the
[native custom-hull reuse plan](predictive-custom-hull-2.0-plan.md), written
before production changes. Native selection/offsets, exact box rejection and
the conservative custom collection envelope are now source-integrated with
final personal Astra Max acceptance. Stock collection remains unchanged;
end-of-implementation qualification is pending. Broader authored state checks remain qualification;
plain replay of arbitrary server-only QC is not implied by the standard builtin.

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
