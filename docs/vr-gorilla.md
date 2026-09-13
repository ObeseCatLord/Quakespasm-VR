# Gorilla movement

Gorilla movement is an optional, engine-side hand-locomotion mode. It needs
support in both the client and server. It does not replace a mod's QuakeC.

## Controls and server policy

- Enable **Gorilla movement** in VR Options, or set `vr_gorilla 1`.
  The client preference defaults to off and is saved in the configuration.
- Servers permit the feature by default. `sv_gorilla 0` disables it for everyone;
  `sv_gorilla 1` permits each VR player to choose it independently.
- Permission is not activation. Clients that keep the default `vr_gorilla 0`
  send the same movement bytes, use the same physics and QuakeC inputs, and do
  not enter the Gorilla hand-simulation queue or receive Gorilla state.
- Plant either hand on solid scenery and push away. Two planted hands average
  their displacement. Stronger strokes produce a capped launch.
- Stick propulsion is suppressed while hand locomotion owns movement. Turning,
  aiming and firing retain their existing controls.
- Actual ladder contact retains native movement and stick controls. Entering
  water or riding a platform does not enable joystick propulsion.
- In deep water, sweep submerged hands backwards to swim forwards, or downwards
  to rise. A quicker power stroke contributes more than a slow recovery stroke.
  Wall/floor pushes also work underwater. Normal water drag, drowning, liquid
  damage and water-exit movement remain owned by the game. When hands are idle,
  ordinary sinking resumes.
- Wind tunnels, jump pads and knockback keep their native momentum. Planted
  palms are soft contacts: only physical strokes add hand propulsion. They
  cannot pin the player against a map's forced movement or instantly arrest
  existing falling momentum. New gravity is suppressed while a palm is braced;
  normal collision and friction continue.

On activation, tracked eyes, hands and the avatar's tracking floor are lowered
together. Initial eye height targets 28 Quake units above the existing body's
feet (about 1.07 metres at the default world scale). This makes the game floor
reachable without requiring the player to crouch down to their physical floor.
The offset is calibrated once on entry, not continuously: subsequent real
crouching remains visible. Disabling the feature restores the normal posture
without rewriting `vr_floor_offset` or weapon calibration.

The normal full Quake player hull is unchanged. The low visual posture is not a
smaller collision box and does not permit entry through smaller openings. This
deliberately avoids falling through gaps, altered pickup ranges and incompatible
player collisions. A palm initially below the game floor is resolved to its
surface without imparting an impulse; further physical hand movement supplies
the push, not the initial penetration distance.

## Hands and moving surfaces

The original Ranger, QBJ3 and Enyo weapon assets have deterministic generated
offhand variants. Mods retaining the original Ranger axe can reuse its hand.
Generation uses recognized source meshes; unsupported or
modified source data fails closed rather than guessing a cut. No extra hand is
drawn when the existing akimbo renderer already draws both hands. The primary
weapon retains its normal model and calibration. Bare-palm button contacts use
the existing verified native button callbacks.

Planted contacts on translating brushes are stored in the brush's local space,
allowing a stroke while a lift/platform moves. Normal platform carriage is not
counted a second time as hand propulsion. Removed or replaced brushes invalidate
their contacts. The legacy server collision path does not rotate brush hulls;
its palm anchors use that same origin-only collision basis even when a brush
visually rotates. This preserves agreement with the native body hull without
disabling hand movement on those platforms. It does not add rotating collision
support to the legacy engine.

## Reference and intentional adaptations

Behavior is based on Another Axiom's publicly released
[GorillaLocomotion](https://github.com/Another-Axiom/GorillaLocomotion), especially
`Player.cs` and its supplied player prefab, with
[GorillaQuake](https://github.com/duncancarroll/gorillaquake) as a Quake reference.
These are references for the published locomotion system, not a claim to
reproduce every change in the current commercial Gorilla Tag game.

The shared controller preserves planted collision-resolved palms, inverse hand
displacement, two-hand averaging, sliding and capped launches. Unlike the
reference's render-frame velocity history, the engine uses a 40 ms command-time
velocity filter so server simulation and client replay use the same time base.
Launch threshold/cap are derived from the reference prefab's 0.4 m/s and 6.5 m/s
values at Quake's default world scale. Quake's unchanged body hull, collision
queries, gravity, gameplay callbacks and network command pipeline remain in use.

GorillaQuake preserves hand pushes against surfaces underwater but still lists
immersive free-water swimming as a TODO. This port's free-water stroke helper
is an additional engine implementation, not a direct port of that feature.
It adds bounded acceleration opposite physical hand velocity, excludes hands
already pushing solid surfaces, and never truncates faster existing velocity.

## Networking and mod compatibility

For native predicted multiplayer movement, `sv_gorilla_trustclient 1` (default)
lets the client solve its own palm contacts once per movement command. It sends
the resulting hand displacement and added propulsion, not an absolute player
position or replacement total velocity. The server clips that displacement
against the normal body hull and preserves its own physics, platform carriage,
knockback and gameplay callbacks. This mode assumes honest clients; it is not
an anti-cheat boundary. `sv_gorilla_trustclient 0` selects the original
server-solved palm path instead.

Trusted contributions are frozen in the existing sequenced command history.
Packet redundancy and prediction replay cannot commit the local hand controller
again; render-only partial movement uses a disposable copy. Small corrections
rebase tracking references and revalidate planted contacts without counting the
correction itself as a hand stroke. Remote VRIK remains cosmetic: its delivery
rate does not determine locomotion or permitted palm-button callbacks.

The optional trusted input occupies 13 bytes with no displacement/impulse,
25 bytes with either vector, or 37 bytes with both. A four-byte server-owned
generation replaces the 95-byte palm-state reply in steady trusted operation.
This generation rejects contributions authored before a server discontinuity;
an ordinary packet gap does not invalidate every pending stroke. Movement ACKs
and their complete owner-body baseline are repeated together in split snapshots,
so losing one packet cannot pair a new ACK with an old player position.

Local singleplayer, legacy physics, custom QC movement ownership, and peers
without the trusted extension retain the original bounded palm/head samples.
That path uses authoritative planted-hand state for prediction and an ordered
auxiliary pose queue for legacy physics, without multiplying QuakeC thinks or
touches. Both paths share the existing controller and native movement adapters.

Death, teleportation, tracking discontinuities and mode changes discard old
contacts. A client's local preference cannot enable the feature against server
policy. Clients not using Gorilla movement retain their original movement data
and physics behavior; desktop and normal VR players can share the same server.

Ladder compatibility is decided from the active program's ladder contract, not
merely its folder name. The installed-mod audit includes QBJ3, Ravenkeep, AD
and the other programs exposing `onladder`, plus Immortal's touch-populated
`laddercount` family. Contact is latched across `PlayerPreThink` because some
programs consume that field there. Ladder cooldown and sound timers alone do
not disable hand movement. New contracts need an entry/exit test.

Custom QuakeC physics callbacks retain their existing movement ownership. The
engine supplies one auxiliary hand step, and an owner delegating to `sv_pmove`
does not receive the same stroke twice. Only joystick movement values are
suppressed; QC-authored changes to other input globals remain intact. This
does not establish compatibility with every possible future replacement
physics implementation without a mod-specific gameplay check.

## Verification boundaries

Private local tests cover shared-controller geometry, below-floor recovery,
posture lowering, generated model geometry, optional wire state and production
PMove against a real BSP, sustained strokes through actual water in both native
movement paths, weak environmental forces, and interpreted QC delegation.
Connected synthetic-input tests additionally cover the real client authoring,
wire and server movement path with an ordinary peer under delay, packet loss
and reordering. They check unique consumption, generation fencing and immutable
replay. Real-BSP tests also exercise native pusher carriage and a contribution
consumed after the brush advances independently. These tests do not establish physical-headset comfort,
hand mesh orientation or multiplayer feel. Before publishing, test real strokes,
moving lifts, ladder entry/exit, death/teleport resets and mixed Gorilla/ordinary
clients in VR. Keep private test fixtures and mod assets out of public packages.
