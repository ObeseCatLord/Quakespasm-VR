# Multiplayer netcode audit, November 2026

Scope: what a player sees and feels in internet co-op on a dedicated server
(40 Hz, 30–150 ms ping, several VR players, large Arcane Dimensions maps).
The reference is QSS-M at `03a498aa` (sibling checkout, branch `qsrebase`);
line numbers for it are given as `QSS-M file:line`. Line numbers for this tree are those of
the commit that adds this document.

Four changes were implemented and measured. None changes the wire format, and
each has a cvar that restores the previous behaviour. A client and a server
of the parent commit were run against this tree in both directions (see
Verification). Everything else is listed as a
recommendation.

## Summary

| # | Finding | Who sees it | Status |
|---|---|---|---|
| 1 | Command-driven players move in whole commands per server frame, so everyone else sees them alternate between about half and full speed | every remote player, even on a perfect link | fixed on the server, `sv_playercarry` |
| 2 | The client clock is forced back between the last two messages on every late or early datagram | everything that is interpolated: players, monsters, projectiles, doors, lifts | fixed on the client, `cl_interp` |
| 3 | A remote VR pose is drawn between the newest two samples by arrival time with a delay longer than their spacing | heads and hands of other VR players | fixed on the client, `cl_vrik_timeline`; not seen in a headset |
| 4 | A lost reliable fragment stalls the whole reliable stream for a flat second | prints, centerprints, scoreboard and state changes, map loads | fixed in the transport, `net_resend_adaptive` |
| 5 | Entities are removed the moment their removal arrives, ahead of the interpolation clock | rockets and nails vanish a little before the wall | recommended |
| 6 | Pose stream is 20 Hz, gated again by the server, and carries no sender time | remote VR hands lag about 85 ms and stall on a lost pose | recommended |
| 7 | Dedicated loop sleeps in 1 ms steps past its deadline | server frames are 25.4–25.6 ms instead of 25.0 | recommended |
| 8 | Prediction errors snap (`cl_predict_smooth 0`) | own view after a misprediction | measured, no change needed on flat ground; recommended follow-up |
| 9 | Server cfg still sets cvars that no longer exist | nobody directly | operational note |
| 10 | Server timestamps are 32-bit floats | very long sessions on one map | note |

## How it was measured

Nothing here was judged by eye.

- **Link impairment** (`net_dgrm.c:598`, `Datagram_ReadLagged` `net_dgrm.c:744`,
  `Datagram_WriteLagged` `net_dgrm.c:724`). New development cvars on a
  connecting client: `net_fakelag` (one-way ms, both directions),
  `net_fakejitter` (0..N ms per datagram), `net_fakeloss` (percent),
  `net_fakeburst "period_ms length_ms"` (inbound datagrams held and released
  together), `net_fakestep "period_ms extra_ms"` (inbound delay that is
  longer for half of every period), `net_fakeseed`. They act on raw datagrams below sequencing and
  acknowledgement, keep datagram order, are seeded so two runs see the same
  sequence, and never act in a dedicated server. All default to off.
  QSS-M has only `pq_lag`, which delays outgoing commands
  (`QSS-M cl_input.c:362,784`). The cvars the old server cfg refers to
  (`net_lagdebug*`) do not exist in this engine.
- **Displayed-position log** (`CL_LerpDebugLog`, `cl_main.c:1984`).
  `cl_lerpdebug_log <file>` writes, per rendered frame, the client clock, the
  message times and the position each entity is actually drawn at (through
  `R_GetEntityLerpedTransform`, so step entities are covered), plus counters
  for every time the clock had to be forced.
- **Scene.** One dedicated server at `sys_ticrate 0.025` with the co-op
  settings of the real server, a second client holding forward and turn so it
  runs a circle at constant speed (72 commands a second), and an observer at
  90 frames a second. A constant-speed mover must be drawn at constant speed,
  so the spread of its per-frame drawn speed is the judder. Both clients were
  admitted to predictive movement (`q30` on the tcj start map, `stock` on
  id1), which is the path the real server uses.
- **Numbers.** `cv` is the standard deviation of the drawn per-frame speed
  over its mean. `stall` and `jump` are the share of frames drawn below 30 %
  or above 170 % of the median speed. `jerk p95` is the 95th percentile of
  the frame-to-frame change of speed, over the mean speed. The mover's true
  speed varies by about 2 %, which is the floor of `cv`. Depending on where
  it spawns, the mover's circle sometimes clips a wall once per lap; such a
  run (mean drawn speed below 366) was repeated so that every row compares
  the same motion.

Everything ran on one machine over localhost on a hidden display with VR
disabled. No headset session, no real internet path and no ARM build were
used; see "Not verified" at the end.

### Result: a remote player as drawn by the observer

Before = `sv_playercarry 0` and `cl_interp 0`, the previous behaviour, same
binary. After = defaults. 20 s per run, id1 `start`.

| Link (observer side unless noted) | cv before | cv after | stall+jump before | after | jerk p95 before | after | clock forced per second before | after |
|---|---|---|---|---|---|---|---|---|
| clean | 0.187 | 0.022 | 0.0 % | 0.0 % | 0.464 | 0.056 | 0.2 | 0.0 |
| 40 ms, 0–10 ms jitter | 0.290 | 0.022 | 4.4 % | 0.0 % | 0.793 | 0.054 | 18.7 | 0.0 |
| 40 ms, 0–20 ms jitter, 3 % loss | 0.439 | 0.024 | 11.5 % | 0.0 % | 1.179 | 0.059 | 24.7 | 0.0 |
| both clients: 30–40 ms, 0–15 ms jitter, 2 % loss | 0.482 | 0.028 | 11.3 % | 0.0 % | 1.192 | 0.058 | 18.2 | 0.0 |
| 40 ms, 0–8 ms jitter, everything held for 120 ms every 2 s | 0.690 | 0.241 | 10.2 % | 3.5 % | 0.801 | 0.070 | 24.9 | 3.9 |
| 0–8 ms jitter, delay switching between 40 and 120 ms every 2 s | 0.461 | 0.266 | 5.9 % | 0.8 % | 0.788 | 0.161 | 18.8 | 3.0 |

The last two rows are disturbances longer than any reasonable buffer. A
120 ms hold still freezes the picture once the margin and the 25 ms of
extrapolation are used up, and when the held updates arrive together the
clock skips to the oldest one it kept; the classic lerp freezes for the
whole hold and then jumps the full distance (largest single-frame speed
10.8 times normal, against 4.8). A delay that changes level costs one such
freeze or skip per change instead of a second of stutter.

The tcj start map (Arcane Dimensions progs, `q30` predictive movement) gives
the same picture; its numbers are in the verification section.

Which change does what, from the two-by-two runs:

| Link | neither | carry only | interpolation only | both |
|---|---|---|---|---|
| clean, cv / jerk p95 | 0.184 / 0.446 | 0.152 / 0.091 | 0.211 / 0.467 | 0.021 / 0.053 |
| 40 ms + 0–10 ms jitter, cv / jerk p95 | 0.297 / 0.791 | 0.216 / 0.701 | 0.182 / 0.446 | 0.021 / 0.048 |

(These four-way runs used an earlier build of the same changes that kept
three updates instead of four; the before and after columns agree with the
final table.)

The carry removes the judder that is there on a perfect link; the buffered
clock removes what the link adds. Each leaves the other's share untouched.

## Findings and changes

### 1. Command-driven players move in whole commands per server frame

**Evidence here.** A predictive-movement player is simulated only when one of
its commands runs: time is credited per server frame
(`sv_phys.c:10431`) and spent per command (`sv_phys.c:11314`). With 72
commands a second against 40 frames, most frames run two commands (27.8 ms of
movement in a 25.5 ms frame) and about one in thirteen runs one (13.9 ms).
The snapshot then carries the raw origin (`SVFTE_BuildSnapshotForClient`,
`sv_main.c:2732`). On a clean local link the observer drew the mover at
409, 409, 409, 219, 204, 334, 409 units per second: a dip to half speed for
one snapshot, about three times a second. With an uneven uplink a frame runs
zero or three commands and the swing grows.

**QSS-M.** Its command-driven path has the same property: commands run on
receipt (`QSS-M sv_user.c:635–692`) and nothing compensates. QSS-M hides it
by default because `sv_nqplayerphysics` is 1 there (`QSS-M sv_user.c:49`),
which keeps players on per-frame physics. This engine runs predictive
movement on the real server, so it shows.

**Change.** `SV_CarryPlayerOrigin` (`sv_main.c:2671`, called at
`sv_main.c:2855`). The unspent command time is exactly how far a player is
behind the server clock, on top of a standing reserve of about 65–90 ms that
the accounting keeps. The function follows the recent minimum of the unspent
time and, in the entity state sent to *other* clients only, carries the
player forward along its velocity by the part above that minimum, through a
hull trace so it stops at walls. The player's own snapshot, collision, hit
detection and QuakeC see the true origin. It applies to live, walking,
predictive-movement players only.

- `sv_playercarry` (default 60): the most that is carried, in ms. 0 restores
  the raw origin.
- Risk: low. What other clients are sent can lead the true origin by at most
  one command interval in normal play (about 4.5 units at running speed) and
  by 60 ms when a player's commands stop arriving; it then waits there and
  the true origin catches up when they resume. A player standing still is
  never moved. Stairs can stop the trace for a frame.
- Not covered: clients on the classic protocol path (they are not
  command-driven), and noclip/fly movement.

### 2. The client clock is forced on every late or early datagram

**Evidence here.** `CL_LerpPoint` (`cl_main.c:2165`) keeps `cl.time` between
the last two message times. A datagram that arrives late leaves the clock at
the newest message and every interpolated entity stops; one that arrives
early sets the clock forward and everything skips. With 10 ms of jitter the
clock was forced 20 times a second. In two of the classic-lerp runs the
forcing also left `cl.time` below the previous frame's value once, which
takes the "time ran backwards" path (`cl_main.c:3627`) and resets every
entity's animation and movement blend for that frame. Step entities
(monsters) glide for 100 ms from the previous update's position, starting
when the datagram is read (`CL_EntityLerpUpdated`, `cl_parse.c:1299`): an update
that arrives early cuts the glide short and the monster jumps to its end, a
late one leaves it standing.

**QSS-M.** Identical in substance: `QSS-M cl_main.c:1180–1223` is the same
function, differing only in not clamping inside the 1 % tolerance, and step
movement restarts at arrival (`QSS-M r_alias.c:2756`). QSS-M has no jitter
buffer; this change goes beyond it rather than porting from it.

**Change.** Buffered interpolation, `CL_LerpPointBuffered`
(`cl_main.c:2075`).

- `cl.time` becomes a steady clock one update interval plus a margin behind
  the newest message. It is steered towards that position by at most 10 % of
  the frame time, jumps forward only when more than 150 ms behind, and is
  never set back.
- Arrivals are compared with the fastest ones seen recently: nothing
  arrives sooner than the route allows, so those mark the route and every
  other arrival is late by some amount. The margin is the recent worst
  lateness (a peak that fades over 5 s), from 4 ms up to two intervals. It
  includes the wait for the next rendered frame, so on a clean link at
  90 frames a second it settles near 13 ms; on a bad link it grows instead
  of stuttering.
- When the route gets faster, the updates run ahead of the clock. The clock
  then stays on the oldest kept update and catches up at 10 %, interpolating
  all the way, instead of stepping.
- Four updates are kept instead of two (`cl.mtime[4]`, per-entity
  `msg_old_origins`, `CL_ShiftEntityHistory` `cl_parse.c:1266`), so the
  clock can sit further back and a datagram can still arrive early.
  `CL_LerpEntity` (`cl_main.c:3265`) interpolates over whichever pair the
  clock is between.
- When the next update is late beyond the margin, positions continue past
  the newest update for up to `cl_interp_extrapolate` ms, then hold. Turns
  are not extrapolated, nor is anything across a gap longer than 100 ms.
- A step entity's glide starts from where the entity is drawn at that
  moment instead of from the previous target, and runs 20 ms longer than the
  nominal step, so an early update only changes its speed slightly and a
  late one no longer leaves it standing (`CL_EntityLerpUpdated`,
  `cl_parse.c:1299`). Animation blends are unchanged. A first version
  that started both at the server time of the previous update was measured
  to be worse for step entities (a jump and a hold on every step) and was
  dropped.
- An entity that just came into view is not drawn until the clock reaches
  the update that introduced it (`cl_main.c:3823`), instead of appearing and
  then waiting at its first position.
- Active only on a remote server: not with a local server, not in demos or
  timedemos (`cl_main.c:2165`). Single player is untouched.

Cvars: `cl_interp` (1; 0 = classic lerp), `cl_interp_margin` (-1 = follow the
measured jitter, otherwise ms), `cl_interp_extrapolate` (25 ms).

Risk and side effects:

- Other entities are shown later than before. Measured as the mean distance
  of the clock behind the newest update: 13 ms with the classic lerp in
  every case; 25 ms on the clean link, 30 ms with 10 ms of jitter and 34 ms
  with 20 ms of jitter and loss with the buffered clock. The margin can grow
  to two update intervals (51 ms at 40 Hz) on links whose jitter is that
  large. The player's own movement is predicted and is not
  delayed.
- The ping the server shows rises by the same margin, because the private
  command timestamp is the client clock (`sv_user.c:1567`). It was already
  inflated by the classic lerp's delay.
- When the own player is not predicted (dead, intermission, the frames
  where the server withholds prediction on a lift) the view follows the
  same buffered clock as the world, so it stays consistent with a lift it is
  riding, but responds later by the margin.
- Extrapolation can overshoot by up to 25 ms of movement when something
  stops in the same instant that a datagram is late; it is then corrected.
- A removed entity still disappears on arrival, now slightly further ahead
  of where it is drawn (finding 5).

### 3. Remote VR poses were drawn with a built-in step

**Evidence here.** A pose is sent at most every 50 ms and only with a
command (`cl_input.c:1039`), so about every 55.5 ms; the server forwards it
on its next frame, so arrivals are two server frames apart, then three. The
viewer kept the newest two, stamped with the frame that read them, and
blended at `realtime - 0.075`. A 75 ms delay over a 55 ms spacing means the
blend sits at zero for the first 20–25 ms after each arrival, reaches only
about half way before the next pose replaces the pair, and then steps the
rest. The fixture reproduces it: a hand moving at constant speed over a
clean link was drawn stopped in 33 % of frames and at more than 170 % speed
in 16 %.

**QSS-M.** No equivalent (no tracked pose transport).

**Change.** `Quake/vrik_interp.h`, used by `CL_ParseVRIKPose`
(`cl_parse.c:665`), `CL_VRIKPlayback` (`cl_parse.c:143`) and the two
samplers (`r_alias.c:675`, `r_alias.c:729`).

- Four poses are kept per player slot (the two on the entity plus a small
  per-slot table; no per-entity memory was added).
- Each pose gets a time on the sender's steady grid, recovered from the
  sequence numbers and a slow average of the arrivals, so the bunching by
  server frames no longer changes the drawn speed.
- Playback runs one pose period plus 30 ms behind that grid (about 85 ms)
  and blends between whichever two poses straddle it.
- A gap, a restarted stream or a new generation starts a new grid.

| Fixture case | two poses, 75 ms (before) | timeline (after) |
|---|---|---|
| clean | cv 1.100, stall 32.9 %, jump 16.4 % | cv 0.024, 0 %, 0 % |
| 5 ms jitter each way | cv 1.091, 33.6 %, 17.2 % | cv 0.023, 0 %, 0 % |
| 15 ms jitter each way | cv 1.123, 33.3 %, 17.1 % | cv 0.029, 0 %, 0 % |
| 10 ms jitter, 5 % pose loss | cv 1.090, 32.0 %, 16.2 % | cv 0.223, 1.3 %, 0.6 % |

Risk: medium, because it is verified by the fixture and by compiling only.
The timing logic is isolated and unit-tested, but `CL_VRIKPlayback`, the
history shift and the two rewritten samplers have not run with a real pose:
no headset session was possible here. `cl_vrik_timeline 0` restores the
previous newest-two sampling. Remote hands are about 10 ms later than
before. A lost pose still shows as a short hold (last row); covering it
needs a longer delay or a faster stream (finding 6).

### 4. A lost reliable fragment stalled the reliable stream for a second

**Evidence here.** Reliable data goes out one fragment at a time and the
next waits for the acknowledgement; the resend timer was a constant 1.0 s
(`net_dgrm.c:1275`, `net_dgrm.c:1310`). Any loss of a fragment or of its
acknowledgement therefore delays everything behind it by a second: prints,
centerprints, name and score updates, and each fragment of a map load.

**QSS-M.** The same constant (`QSS-M net_dgrm.c:1046,1076`).

**Change.** `Datagram_ResendDelay` and `Datagram_ReliableAcked`
(`net_dgrm.c:556`, `net_dgrm.c:564`). The time from sending a fragment to its
acknowledgement is measured (skipping fragments that were resent) and the
resend delay becomes twice that plus 100 ms, between 0.2 s and the old 1 s.
Until a measurement exists it is 1 s. A resend the peer did not need is
recognised by its sequence number and only acknowledged again, as before.
Both ends benefit independently; nothing is negotiated.

Connecting on the tcj start map at 50 ms and 20 % inbound loss, six seeds
each, took 6.7–13.0 s with the fixed second (mean 9.6 s) and 3.9–6.4 s with
the adaptive timer (mean 4.9 s). `net_resend_adaptive 0` restores the
constant.

Risk: low. The worst case is a duplicate fragment on a link whose round trip
suddenly grows.

### 5. Removals are not buffered (recommended)

An entity is dropped as soon as a message no longer contains it
(`cl_main.c:3602`), while its drawn position is one interval plus the margin
in the past. A rocket therefore vanishes before it reaches the wall while
the explosion appears at the wall: about 25 units short with the classic
lerp, 30–40 with the buffered clock on a good link. QSS-M behaves like the
classic case. The fix is to keep a removed entity until the clock reaches
the message that removed it, and to give temporary entities the same delay.
It touches entity lifetime and the BLAS bookkeeping, so it was left out of
this low-risk pass.

### 6. Pose stream cadence (recommended)

The sender's 20 Hz gate (`cl_input.c:1039`) is quantised to its command
rate, the server accepts at most one pose per 25 ms and reads them once per
frame (`sv_user.c:1979`, `protocol.h:45`), and the pose carries a sequence
but no time. The timeline in finding 3 recovers the grid, but a faster
stream (30–36 Hz) would shorten the playback delay from about 85 ms to about
60 ms and make a lost pose invisible. That costs bandwidth per VR player per
viewer and should be gated by the existing pose capability negotiation.

### 7. Dedicated server pacing (recommended)

`main_sdl.c:125` sleeps in 1 ms steps until a full `sys_ticrate` has passed
since the previous frame started, so every frame overshoots: measured
25.4–25.6 ms, about 39.2 Hz. QSS-M uses the same loop
(`QSS-M main_sdl.c:146`) with a 20 Hz default (`QSS-M host.c:86`). Server
time follows real time, so nothing drifts, and the buffered client clock
absorbs the wobble. Scheduling against absolute deadlines would give an
exact 40 Hz. Related: the client sends at 72 Hz (`quakedef.h:84`) against a
40 Hz server, which is the beat behind finding 1; it needs no change now
that the carry exists.

### 8. Own view under prediction

The predicted own view was measured running the same circle under 40 ms,
0–15 ms jitter and 3 % loss: cv 0.027 and jerk p95 0.073 in every
combination of `cl_interp` and `cl_predict_smooth`. Prediction does not use
the interpolation clock, and on flat ground there were no corrections to
smooth. `cl_predict_smooth` stays 0 as decided in
`prediction-smoothing-2.0-plan.md`. Worth measuring separately with the same
log: stairs, lifts and being pushed, where corrections do occur.

### 9. Stale server settings (operational)

The local copy of the co-op server cfg sets `sv_maxpacketsize`,
`sv_replacement_maxpackets`, `sv_pmove_mode`, `sv_nqplayerphysics`,
`sv_trustedmovement`, `sv_predict_nqmovement`, `sv_inputtimeout`,
`sv_pmove_legacy_preserve_qc_velocity`, `net_sameip_stale_timeout` and the
`net_lagdebug*` cvars. None of them is registered by this engine (of its
network settings only `sv_netsort` is; the server prints "Unknown command"
for the others). The datagram limit is the
compile-time `DATAGRAM_MTU 1400` (`quakedef.h:89`, same as
`QSS-M quakedef.h:112`), and a snapshot that does not fit is continued in
further datagrams the same frame (`sv_main.c:4935`, as in
`QSS-M sv_main.c:3447`), so no entity is starved. Predictive movement was
selected for every test client without `sv_pmove_mode` existing. The lines
are harmless but misleading.

### 10. Smaller notes

- **Timestamps.** Snapshot times are 32-bit floats (`sv_main.c:2230`). After
  about 4.5 hours on one map their resolution is 2 ms, which adds that much
  jitter to the interpolation timeline. Map changes reset it.
- **Loss recovery of entity state** is acknowledgement-driven as in QSS-M
  and resends only what was in the lost datagram; new entities are sent
  twice. The private command packet repeats recent commands, which QSS-M
  does not do (`migration-network-map.md`, NET-010). No change needed.
- **Reliable overflow** disconnects the client as upstream does; the 64 000
  byte buffer was not seen to fill in these tests.
- **Server frame loop.** Client input is read once per server frame and
  snapshots leave at the end of it; with a listen server the loop at
  `host.c:1334` can run several catch-up passes in one rendered frame, where
  QSS-M runs one (`QSS-M host.c:2321`). Neither affects a dedicated server.

## Verification

Build: `meson setup build-net`, `ninja -C build-net`, default options
(`-Werror`). With GCC 16 the tree needs
`-Dc_args=-Wno-error=dangling-pointer` because of an existing
warning in `sv_phys.c:6230`; that file was not changed.

Fixtures:

- `tests/vrik_interp_fixture.c` (new) passes; its header has the compile
  line.
- `tests/datagram_rebind_fixture.c` passes with four added link stubs.
- `client_input_preview`, `client_replay`, `client_replay_solver`,
  `client_public_preview` and `private_owner_snapshot` already fail to build
  at the parent commit for unrelated reasons and were not repaired here.

Runtime, same binary with the cvars switched:

- id1 `start`, five link conditions, before and after: table above.
- tcj `start` (`q30` predictive movement), before and after:

  | Link | cv | stall+jump | jerk p95 | clock forced per second |
  |---|---|---|---|---|
  | clean, before | 0.274 | 0.0 % | 0.603 | 0.0 |
  | clean, after | 0.026 | 0.0 % | 0.062 | 0.0 |
  | both clients impaired, before | 0.566 | 17.5 % | 1.405 | 18.8 |
  | both clients impaired, after | 0.048 | 0.0 % | 0.085 | 0.0 |

- A walking monster in view of an idle observer on `tcj_bisquit` (one step
  entity, about 2600 frames per run). Its speed changes with every
  animation frame, so `cv` has a high floor here; `jump` and the jerk are the
  telling columns.

  | Link | cv | stall | jump | jerk mean | jerk p95 |
  |---|---|---|---|---|---|
  | clean, before | 0.356 | 3.3 % | 0.7 % | 0.093 | 0.336 |
  | clean, after | 0.219 | 1.7 % | 0.0 % | 0.025 | 0.230 |
  | 40 ms, 0–10 ms jitter, before | 0.452 | 6.1 % | 4.2 % | 0.291 | 1.136 |
  | 40 ms, 0–10 ms jitter, after | 0.235 | 1.8 % | 0.0 % | 0.029 | 0.245 |

- Mixed versions, with the installed build of the parent commit as the other
  side, 40 ms and 0–10 ms jitter on the observer: a parent server with a
  parent mover and a new observer (cv 0.186, no stalls or jumps, clock never
  forced: the buffered clock alone, as the parent server has no carry), and
  a new server with a parent mover and a new observer (cv 0.063, jerk p95
  0.057). Both stayed connected with no errors on either side.
- Own predicted view: finding 8.
- Connecting on tcj `start` at 50 ms and 20 % inbound loss, six seeds per
  setting (about 2.7 s without loss):

  | Resend timer | seconds to enter the game | mean |
  |---|---|---|
  | fixed 1 s (`net_resend_adaptive 0`) | 8.6, 11.4, 10.0, 7.7, 6.7, 13.0 | 9.6 |
  | adaptive (default) | 6.4, 5.6, 4.4, 3.9, 4.6, 4.3 | 4.9 |

### Not verified

- No headset was used. The pose timeline is verified by the fixture and by
  compiling and running the desktop client, not by watching a remote avatar.
- Step entities were measured on one monster on one map. Animation timing
  was left as it was.
- Real internet paths, more than two clients, and the ARM64 server build.
- Riding a lift or standing on a moving door, where the predicted player
  meets a delayed world.
- Maps large enough to split a snapshot over many datagrams were loaded only
  as far as the tcj start map.
