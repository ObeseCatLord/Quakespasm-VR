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
Windows/ARM verification remains deferred. The full remaining migration scope
is unchanged.

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
