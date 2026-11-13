# Gorilla ("monke") movement modelled on Ancient Dungeon VR

The earlier 2.0 hand solver (a defensive port of Gorilla Tag's `Player.cs`)
was hard to push around with in play. This change replaces its per-command
step with the behaviour of Ancient Dungeon VR's "monke" movement mode, which
the user considers the reference feel. Interfaces, network state layout,
moving-brush anchors, swimming and the server/prediction plumbing are kept.

## What Ancient Dungeon does (reverse engineered)

Source: `ADVR.dll` (Mono) decompiled with ILSpy 11.2; serialized tuning read
from `resources.assets` (GorillaLocomotion MonoBehaviour, path id 4648) and the
PlayerController prefab via UnityPy. Only behaviour and numbers were taken.

- `GorillaLocomotion.Update` is Gorilla Tag's algorithm with additions:
  - each hand's sweep adds a virtual drop of `2·g·dt²` so resting palms stay in contact;
  - any hand or body step over 0.2 m in one frame is treated as a teleport
    (movement ignored, history paused for 2–3 frames);
  - while any palm touches, it zeroes the fling velocity and gravity, then sets
    fling = `1.25 × average body velocity` (6-frame history), capped at 7 m/s,
    whenever that average exceeds 0.05 m/s;
  - planted palms move the body 1:1 (sum for one hand, average for two).
- The fling outruns the pushing hand, so the palm lifts off at peak speed.
  That emergent release is what makes pushes feel strong.
- PlayerController applies the fling only above 0.8 m/s. It decays at 0.25/s in
  the air and 4.5/s (7.5/s in monke mode) on the ground. Gravity is 9 m/s², capped at 30 m/s.
- Monke mode lowers the whole VR rig so the head sits 0.58 m above the floor
  (relaxed standing hands rest on the ground). Stick walking keeps working at 20% speed.
- Other serialized values: arm length 1.75 m, unstick 1 m, palm radius 0.05 m,
  slide factor 0.02 (two hands) / 0.001 (one hand).
- Multiplayer: the host's lobby flag `allowMonke` (default true in AD) is
  stored in session properties. Clients that join a room with it false are
  forced back to smooth movement, and the menu button is disabled.
- Networking (Photon Fusion): `GorillaLocomotion` runs only where the player
  has state authority (`HasStateAuthority`) and destroys itself on every other
  copy. Each player owns its own body, so hand pushes never wait for the network.

## Mapping to Quake

| AD behaviour | 2.0 implementation |
| --- | --- |
| Per-frame solver | `VRG_Step` in `Quake/vr_gorilla.h`, shared by server physics and PMove prediction |
| Fling = 1.25× smoothed velocity while touching | Same. The six-frame history is an EMA (τ 33 ms) kept in the existing `state.velocity` ACK slot |
| AD gravity 9 m/s² vs Quake 800 u/s² | Launch speeds are scaled by `sqrt(g/g_ad)` (~1.84 at stock gravity), so arcs keep AD's shape. Ballistic motion is divided back out of the history to avoid compounding |
| Zero fling/gravity while touching | Velocity is replaced by the fling (or zeroed). `braced` skips native gravity |
| 0.2 m teleport guard | Per-command limit `max(0.2 m, 30 m/s·dt)`, then rebase |
| Head at 0.58 m | `vr_gorilla_drop` (metres, default 1.0) lowers the rig through `V_VRFloorOffset()` while active, which moves camera, head, hands, VRIK and samples together |
| Stick at 20% | `VR_GORILLA_STICK_SCALE` (server and PMove) instead of zeroing stick input |
| Palms on surfaces | Weapon/hand collision retraction is disabled while hand movement is active (`VR_WeaponCollisionAuthorized`) |
| `allowMonke` host flag | `sv_gorilla`: `-1` default = allowed only in local single-player, off on multiplayer servers; `0` off; `1` allowed. Refused clients send no hand samples and use ordinary movement. The VR menu shows "server off" and the console prints a notice |

## Owner-authoritative networking (AD's model)

When `sv_gorilla` allows hand movement and `sv_gorilla_trustclient` is on
(default 1), an opted-in VR client owns its own body, as in AD:

- The server offers `//vr_gorilla_protocol 1 2` (0 off, 1 raw hands only, 2
  raw hands plus ownership). The client replies `vr_gorilla_cap 2`.
- Each sent command, `CL_MonkeOwnerAuthor` (`cl_main.c`) runs PMove plus
  `VRG_Step` on the client's own body state. The camera uses that state
  (`CL_MonkeOwnerView`), so there is no network latency on pushes. The command
  carries a 29-byte owner report: flags (active, on ground), generation,
  origin and velocity (`MOVEEXT_GORILLA_TRUSTED`).
- `SV_GorillaOwnerApply` (`sv_phys.c`) accepts a report only for the current
  generation. Each report spends a movement budget that accrues from server
  time (1.5× the faster of `sv_maxspeed` and the hand solver's fling cap,
  bursting up to 0.25 s), so sending more or larger reports buys no extra
  speed. Falling uses what gravity allows on top of that. The path is swept
  in hull-sized steps: solid entities it reaches are touched (buttons, key
  doors) and triggers along it fire in order, so thin triggers cannot be
  skipped. The reported velocity is capped at the same speed and cannot hide
  a fall from QuakeC fall damage. Ground contact is re-checked. Native
  movement, room-scale and `SV_ClientThink` input are skipped for that
  player. QuakeC Pre/PostThink, triggers, damage and weapons stay
  server-owned.
- Swimming (`waterlevel` ≥ 2) and standing on a moving pusher hand the body
  back to server movement with the raw-hand solver; ownership resumes 0.25 s
  after that ends. Wading slows the body in QuakeC; that damping is accepted
  instead of forcing a resync.
- The raw-hand (non-owner) solver spends a server time credit per sample, so
  extra or short commands cannot speed it up either.
- On the client, the view is carried along the body's velocity between
  commands (up to 50 ms, traced against the world), so headsets refreshing
  faster than the command rate see smooth motion. Reseeds within 64 units
  blend into the view over 100 ms, and brief hand-tracking loss (up to
  0.25 s) keeps owner mode instead of falling back.
- If the server moves the body itself (teleporters, knockback, QuakeC jumps,
  pushers, noclip/death), or a report fails a check, it starts a new
  generation. The generation goes out in every move ACK
  (`MOVEACK_FLAG_GORILLA_TRUSTED`), and the client reseeds from the server's
  entity state. Old-generation reports are ignored. Reports stop counting
  0.25 s after the last packet.
- The server sends its movement constants to owners
  (`SVFTE_WritePrivateMoveStats`), so both sides simulate with the same
  physics.
- The existing predicted stock-id1 trial path (`sv_private_pmove_walk`) keeps
  its own raw-hand replay. Owner mode applies everywhere else (mods, listen
  servers and fallback frames).
- Desktop players, regular VR players and players without the option never
  take any of these paths. Their packets and movement are unchanged.
  `vr_gorilla` only takes effect in a live VR session (`VR_GorillaActive`), so
  a desktop player with the option saved still plays ordinary desktop Quake,
  with no notice, posture or weapon changes.
- Trust model: as in AD, a modified client can move its own body freely within
  the speed and collision checks. Set `sv_gorilla_trustclient 0` to keep monke
  movement server-authoritative (raw hands, with prediction only on the trial
  path).
- Developer test aid: with `developer 1`, `vr_gorilla_test_hands 1` supplies
  idle hands and `2` a scripted knuckle-walk, so the path can be exercised on
  desktop.

## Verification and limits

- Linux Meson build is clean. A dedicated server loads e1m1 with the new
  `sv_gorilla` default of `-1`.
- `vr_input_continuity_fixture` passes. `client_replay`, `client_input_preview`
  and `private_moveack` fixtures fail to link at the untouched HEAD too (stale
  stubs), so they could not be used.
- A scratch harness (flat floor, wall, scripted strokes) shows floor pushes
  launching about 150 u/s upward, knuckle rowing building forward speed up to
  the cap, and idle hands doing nothing.
- Networked tests (local dedicated server plus desktop clients using
  `vr_gorilla_test_hands 2`): the owner knuckle-walk moved the body on the
  server, and server noclip produced exactly one resync with a clean reseed.
  A plain client on the same server was unaffected. On a default multiplayer
  server, an opted-in client prints the "off on this server" notice and uses
  normal movement.
- Foundry (aarch64, internet, UDP 26000 test instance): a monke client and a
  plain client on the same server. The monke owner moved the body on the
  server, noclip produced one resync, and the plain client was unaffected. A
  desktop client with `vr_gorilla 1` showed no Gorilla activity.
- No headset run yet. Feel, the default drop, and launch scaling still need tuning in VR.
