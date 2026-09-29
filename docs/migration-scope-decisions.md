# Current migration scope decisions

These user decisions supersede older inventories, plans and historical review
checkpoints. They change the completion requirements, rather than certify work.

## Locomotion exclusions (2026-09-28)

Gorilla locomotion and instant stop are excluded from the current migration
goal. Do not implement new cooperative-QC adapters for either feature or make
their further integration/qualification a completion gate.

- MOVE-009, MOVE-010 and MOVE-012 and the Gorilla-specific part of NET-025 are
  deferred. MOVE-011's physical swim strokes/hand propulsion are deferred too.
- VR-008's instant-stop option is deferred. Joystick response, movement modes,
  head/hand-relative movement and ordinary release/friction remain required.
- Ordinary VR swimming, ladders, roomscale, native momentum, physical combat,
  prediction, desktop play and mod compatibility remain in scope. Excluding
  propulsion does not exclude tracked hands used for weapons or interaction.
- Existing code and wire validation are retained unless a concrete integration
  defect requires a narrow change. No deletion of adjacent working owners or
  protocol bodies is implied by these exclusions.

Skyrooms and quad views are excluded from the current 2.0 goal. The
[Frame/large-map assessment](openxr-quad-views-2.0-assessment.md) records why
two-view foveation remains the selected design. User live headset, eye-tracking,
multiplayer and performance tests are outside this implementation goal;
Windows verification remains deferred. Linux/ARM software verification follows
implementation as recorded below.

## Microphone selection and defaults (2026-09-29)

Use the system default recording device. AUDIO-003's inherited headset
endpoint/name matching is superseded; explicit SDL device selection remains
available without headset-specific routing. VR transmission is default on and
opt out, with a valid saved opt-out retained. Desktop transmission retains its
existing opt-in behavior. Local microphone reflections remain independent.
The [bounded voice plan](voice-default-device-2.0-plan.md) records reuse of the
existing SDL capture/settings owners and mode-transition behavior.

## Remaining migration work and platform order (2026-09-29)

The user authorized continuing the remaining migration steps, copying existing
implementations and adapting their boundaries. Mjolnir's dual-state weapons
are excluded from this implementation goal: do not expand its combined native
trigger/physical-contact modes (including the projectile/stab and charged
hybrid adapters) to close the inventory. Ordinary weapon presentation, native
trigger play, Mjolnir map compatibility and large-map performance remain required.

Windows builds remain deferred. Complete implementation before consolidated
verification; use this Linux machine and the authorized Foundry Linux ARM host
for that verification. Do not deploy to or modify its running game/server merely
to build. Windows remains a release target, not a required build in this pass.

## Universal melee fallback (2026-09-29)

Uncovered mods should have built-in melee behavior without requiring engine
patches per mod. The [generic melee plan](migration-generic-melee-2.0-plan.md)
selects an ordinary attack-input gesture: default standard-axe conventions and
explicit weapon-profile opt-in for other models. Original QuakeC owns damage,
cooldown, reach and custom effects. No universal damage replacement or arbitrary
QC function invocation is implied. [Configuration](generic-melee.md) records
the limits. The gesture-only decision below supersedes physical-contact work.

## Gesture-only melee (2026-09-29)

The user deferred physical-contact melee implementation and requested only
gestures that activate the mod's ordinary melee attack. In immersive melee
mode the physical attack trigger must do nothing for recognized melee weapons;
only a validated swing supplies the normal attack input. The held VR melee
weapon stays in its ready pose instead of playing a scripted attack animation.
QuakeC retains its internal animation/think sequence, timing, damage and effects;
render-only suppression must not cancel gameplay. Desktop, ranged weapons and
immersive-melee-off input remain native.

MOVE-002's physical reach/contact solver, MOVE-004..007's remaining exact attack
adapters and MOVE-008's physical parry/contact damage are deferred. Existing
server owners and protocol validation are retained, but this client must not
request immersive physical-contact damage. Visual weapon collision, tracked
weapon presentation and native ranged/paired weapons remain in scope. No new
Copper server adapter will be integrated; its uncommitted patch was removed.
This does not remove ordinary native melee play or Mjolnir map support.

The [gesture-only plan](migration-gesture-only-melee-2.0-plan.md) precedes the
client policy, command-finalization and ready-pose changes. Unknown weapons use
the shared profile opt-in rather than a per-mod engine patch. Linux/ARM checks
remain end-of-goal work; live headset testing remains user-deferred.
