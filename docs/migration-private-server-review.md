# Private VR server migration: Astra senior disposition

Reviewed 2026-09-22 against `2.0` and pinned OpenVR `1327f795`. The local
review ran with verified `gpt-6-astra` / `max` settings. This is a design gate,
not an implemented server-compatibility claim. The current client can fire at
the unchanged pinned dedicated server; `2.0` still routes local `clc_move` to
vkQuake's public reader despite having `SV_ReadPrivateUsercmd`.

## Decision

Keep vkQuake's transport, snapshots, QC lifecycle, collision and host cadence.
Admit an explicit per-connection private dialect through the existing `pext`
roundtrip and serverinfo, then adapt those owners only where the pinned wire
layout actually differs. The alternative of copying the inherited server
loop would duplicate public parsing, command lifetime and physics policy.

| Astra recommendation | Main disposition |
| --- | --- |
| Merge admission and version compatibility into one per-connection wire profile; never infer it from colliding PEXT2 bits. | **Adopted.** Use an explicit key/version in the existing `pext` exchange and a serverinfo marker before decoding private extensions. Keep `connect … qsvr1` as the current explicit legacy fallback. The local listen path must use the same handshake as dedicated, not a direct client-state shortcut. |
| Route the exact engine `pext` command before `SV_ParseClientCommand`. | **Adopted.** Current `sv_user.c` offers most client strings to QC first; a mod could swallow negotiation. Preserve QC handling for unrelated commands. |
| Include server-to-client framing in the first slice. | **Adopted.** Private signon `svc_time` has no inline ACK, while vkQuake emits one for PREDINFO. Private replacement snapshots require ACK metadata beyond vkQuake's short ACK. Adapt both serializers and split-packet accounting per connection; do not claim success from decoding `clc_move` alone. Start with legacy-frame authority and prediction disabled until owner/ACK coherence is demonstrated. |
| Use the donor's latest-command mode before PMove queue. | **Adopted as the first behavior proof, not final network parity.** Validate `msec` but do not add a simulation clock. Decode every complete bundled command, discard stale sequences only after consuming their bytes, accumulate each fresh roomscale delta once, retain newest axes/pose, and latch attack/jump plus last nonzero impulse until a real physics/QC pass. Reset on map/reconnect and clear stale input by arrival time. The eventual QSS-M prediction/PMove requirement remains open. |
| Port roomscale through the existing collision owner with controlled callbacks. | **Adopted.** Consume blocked/frozen tracking without later debt. Keep vkQuake solver/pushers and suppress duplicate impact/trigger QC callbacks for the extra physical translation. |
| Scope muzzle/aim across scheduled weapon thinks and PostThink, not just a vanilla fire call. | **Adopted.** Keep accepted-command authority for dedicated and listen. Preserve aim globals, source offset compensation, world clamp and body restoration while respecting QC `setorigin`; actual trace/damage is the proof. |
| Prefer a wholesale inherited server transplant. | **Rejected.** No demonstrated incompatibility requires replacing vkQuake's public network or physics owners. Reopen this decision only if the adapter begins duplicating their state machines. |

The first vertical proof is a `2.0` private client and dedicated server with a
public desktop observer: unobstructed body movement, wall-blocked and frozen
movement without debt, then a calibrated shotgun shot whose actual trace,
target damage, ammo and restored body origin match the accepted pose. Repeat
on listen server, redundant/lost bundles, map change and reconnect before
claiming this slice ready. A decoded packet, ACK counter or shell decrement
alone does not prove collision or muzzle behavior.

The reviewer flagged the QSS `0x80` collision as outside its scoped headers;
main verified it in pinned `QSS-M/Quake/protocol.h:75` (`PEXT2_INFOBLOBS`) and
private `svc57` versus QSS `svcdp_entities` at `:360`. The source tie is a
reason for explicit identity, not for changing vkQuake's public PEXT2 meanings.

No user choice blocks implementation. Supporting an unmodified inherited
client on a new `2.0` server is a separate compatibility expansion; current
`connect … qsvr1` support targets the unchanged pinned server.

## Negotiation checkpoint

The `2.0` client now offers a separate `QSVR` key/version in the ordinary
`cmd pext` response. The server records that offer per connection, and the
client parser accepts a matching serverinfo marker before private PEXT2 bits.
Public PEXT2 support and the default server response remain unchanged. The
server does **not yet select** the profile. Private outbound ACK/signon framing,
a latest-command receiver and a collision-solver roomscale adapter are staged;
their actual gameplay behavior still needs the coupled vertical proof above.
The extra client offer is ignored by public servers. This receiver is the
first compatibility step, not the requested QSS-M-style predictive netcode:
command replay, owner snapshot
coherence, loss recovery and VR-aware prediction remain required.

The inherited `0x40` private extension names a tagged `UF_SOLID` encoding.
vkQuake currently does not emit `UF_SOLID`, so selecting the private profile
must not be treated as proof of predicted collision parity. Keep prediction
disabled in the first proof and add a scoped solid writer only if the owner
snapshot or later prediction path actually needs it.

## Roomscale collision: Astra senior disposition

A second local `gpt-6-astra`/`max` review verified the inherited auxiliary
roomscale call and vkQuake's walk/step/pusher path before critiquing the
adapter. This is a design decision, not a collision-parity claim.

| Recommendation | Disposition |
| --- | --- |
| Reuse vkQuake's walk/fly solver with an explicit callback policy through its step, unstick and push calls. | **Implemented, unqualified.** Ordinary calls retain their current impact/trigger behavior. The auxiliary call suppresses QC callbacks but retains intermediate `SV_LinkEdict(false)` spatial updates. No copied solver or global suppression mode. |
| Apply roomscale before the normal move-frame capture. | **Implemented, unqualified.** A temporary auxiliary pusher frame is used, then velocity/flags/groundentity are restored and the entity relinked without triggers before normal capture. Reusing the old frame after leaving a platform risks stale support. |
| Use one accepted-command authority for remote and listen clients initially. | **Adopted with a parity caveat.** The donor's local direct tracking move runs later and has different callbacks/downward velocity. Measure local latency and behavior; a distinct local path needs evidence before addition. |
| Require one impact/trigger event globally. | **Rejected.** Native vkQuake stepping, unsticking and force-retouch can legitimately call more than once. The invariant is zero **auxiliary** QC callbacks while preserving the normal pass. |
| Fix the walk waterjump check's use of the last global `sv_player`. | **Adopted and implemented.** Read `ent->v.flags` for the entity being moved; otherwise another client's waterjump could affect stepping. |

The next adapter proof covers floor and wall movement, accepted/rejected steps,
startsolid, platform departure/boarding and fast pushers; frozen/inactive input
must leave no tracking debt. Compare dedicated and listen sessions with donor
remote/latest behavior, and count auxiliary versus normal QC callbacks.
Auxiliary-only solid impacts and thin crossed triggers are explicit caveats to
test, not reasons to add a new contact queue before evidence.

## Weapon pose scope: Astra senior disposition

A local `gpt-6-astra`/`xhigh` review compared the staged generic weapon scope
with the pinned OpenVR donor and vkQuake's QuakeC and spatial-link owners. It
found two correctness issues before private-profile activation. The review was
static; a successful build is not gameplay proof.

| Finding / recommendation | Disposition |
| --- | --- |
| An equality guard on `setorigin` can erase a QC teleport after `self.origin` was assigned first. Preserve every explicit `setorigin`, as the donor does. | **Fixed.** `PF_setorigin` marks relocation for the scoped player regardless of its current mutable origin. Other entities do not alter that scope. |
| `setsize`, `setmodel` or another scoped relink can index the temporary hand origin; restoring the body alone leaves stale absolute bounds, area links and PVS leaves. | **Fixed at the shared link owner.** `SV_LinkEdict` records a relink only for a scoped entity. Scope exit re-links the restored body without trigger callbacks, while retaining QC's size/model changes. It does not re-link when an explicit `setorigin` must be preserved. |
| Replace vkQuake physics/QC cadence or add a second local weapon policy. | **Rejected.** The accepted command supplies both local and remote VR pose. vkQuake movement and QuakeC callbacks remain the behavioral owners. |
| Reuse donor source compensation and load weapon calibration on dedicated servers. | **Adopted, still unqualified at runtime.** The helper uses the single weapon schema and the donor's default 8-forward/16-world-up source, grenade self-origin and schema overrides. |
| Activate the private profile now. | **Deferred.** The coupled receiver, collision, weapon, snapshot/ACK and prediction proof remains incomplete. |

Before activation, exercise a shotgun shot against a target and verify trace,
damage, ammo, cadence and restored body state. Cover scheduled continuous fire,
grenades, schema pitch/roll, wall-clamped muzzle, dedicated game switching,
callback-error unwind, and QC `setorigin` after direct assignment in both
Think and PostThink. Also change player size/model during a displaced PostThink
and immediately verify collision bounds, area membership and visibility leaves
without extra trigger callbacks. These checks should run against the pinned
donor where its behavior is the reference.

## Private snapshot collision bounds checkpoint

The private replacement-snapshot writer now derives `UF_SOLID` from the
authoritative edict bounds and uses the pinned donor's tagged solid encoding.
The explicit `QSVR` profile gates both the delta bit and the tagged payload;
public vkQuake snapshots and baselines retain their existing layout. Owned
entities are non-solid to their owner, as in the donor. This supplies the
client collision collector with a server source. The profile is now admitted
only on opt-in servers, but moving-solid writer/decoder roundtrips have not
been qualified.
Before enabling prediction, verify moving and changing solid boxes, BSPs,
owner exclusion, packet loss and reset baselines through a live private
snapshot, alongside the accepted-command owner and ACK association.

## Predictive server movement: Astra senior disposition

A local `gpt-6-astra`/`max` review checked the proposed QSS-M-style private
server adapter against pinned QSS-M `03a498aa`, inherited OpenVR `1327f795`
and the current `2.0` owners. Prediction must be defined by executed commands
and coherent owner snapshots, not by matching either donor's wire bytes.

| Recommendation | Disposition |
| --- | --- |
| Keep public vkQuake physics, transport and QC cadence; add a private movement adapter at the existing player owner. | **Adopt.** QSS-M's solver is already linked for client replay. A wholesale inherited server transplant would import unrelated co-op/Gorilla/mod policy and duplicate working public state. Reopen if the narrow adapter begins reproducing most donor policy. |
| Preserve ordered accepted commands separately from completed or explicitly discarded commands. | **Adopt.** The current latest-command receiver advances `lastmovemessage` on reception and is only a legacy-frame path. PMove cannot acknowledge that sequence as simulated until its physics pass has actually retired it. Bound queue count and total duration; clear it on map, reconnect, pause/discontinuity and overflow without replay debt. |
| Keep both frame roomscale sweep and PMove roomscale movement. | **Reject.** Only the selected movement owner may consume each accepted tracking delta. Likewise, `SV_ClientThink` must not apply native acceleration before PMove. |
| Treat an ACK and arbitrary owner delta as sufficient to permit client replay. | **Reject.** The current client gate requires a matching reset-decoded owner update, accepted authority/epochs/settings and prediction permission. Reserve snapshot space and associate the ACK with completed commands; local listen prediction needs a real server completion rather than a synthetic local ACK. |
| Preserve attack/jump/impulse as bits only while replacing pose with the latest command. | **Reject for PMove.** Retain the originating accepted command so QuakeC weapon callbacks use its hand pose. Legacy latest-command latches remain limited to the legacy owner. |
| Select the private profile for an opt-in legacy-frame proof before enabling PMove prediction. | **Adopt as an explicit qualification checkpoint.** Prediction remains disabled. Profile selection still requires exact marker/layout, private sender/receiver, weapon and collision behavior, signon and snapshot proof with public observers. |

The first implementation slice is a bounded private accepted-command queue in
`server.h`/`sv_user.c`, with separate receive and retirement cursors, exact
duration/action/pose/roomscale records and explicit discard cutoff. It must not
activate prediction by itself. The execution consumer then adapts vkQuake's
player physics/QC owner, followed by matching movement stats, reset owner
snapshots and ACK/epoch publication. Empty queues must remain under the chosen
PMove owner instead of silently switching back to legacy physics. Validate the
whole path locally with jump/swim/platform/contact, earliest-pose weapon damage,
once-only roomscale, packet loss, pause/teleport, listen and remote peers, and
mixed public/private clients. Custom-physics mods are a separate compatibility
gate with prediction disabled until their authority can be stated exactly.

## Explicit legacy-profile admission checkpoint

The server now has a default-off `sv_qsvr_private` opt-in. It selects the
private profile only after an explicit version-1 `QSVR` offer and matching
replacement/PREDINFO capability on an RMQ float-coordinate/short-angle map.
The selected private mask is separate from the retained public offer, so a
later map can return to public framing. Public-only and default-off peers keep
vkQuake's original profile. No predictive authority is advertised.

An isolated Linux loopback test with the `2.0` client and dedicated server
using disposable Straight asset profiles reached signon and observed
authoritative movement, shell consumption and advancing ACK with prediction
permission off. The same client reached public signon and moved/fired when the
server cvar was off. A connected private client also completed `e1m1` to
`e1m2` changelevel after the cvar switched off and parsed the new public
profile. The focused calibration fixture passed ASan/UBSan and the strict
Linux Ninja build passed. These checks do not prove target damage, roomscale
collision, ordered PMove prediction, mixed simultaneous peers, packet loss,
headset input, Windows or ARM behavior.

## Private legacy ACK retirement checkpoint

The private replacement-snapshot ACK now reports a separate completed-move
cursor. The player physics owner captures the latest accepted sequence at
entry and publishes it only after its QuakeC PostThink returns for a live,
spawned owner. Receive-time `lastmovemessage` remains the deduplication cursor;
public PREDINFO ACK framing is unchanged. Both cursors reset on new signon,
replacement-frame setup and client connection. This closes the known case
where a received command was acknowledged before a player physics pass, but
it does not associate a queued command with a predictive PMove step. Prediction
permission remains off. The strict Linux `vkquake` target builds; a focused
runtime stall probe remains to be run. A fresh private-profile loopback run
after this change reached signon, moved 267 units, consumed four shells and
advanced the bounded ACK from 69 to 284 while prediction stayed disabled.
That observes normal frame retirement, not the no-physics stall case.

## Bounded accepted-command journal checkpoint

The explicit private receiver now retains each fresh validated `usercmd_t` in
a 32-entry, 250 ms per-client journal, preserving its sequence, action, VR pose,
roomscale delta and server receipt time. A separate receive cursor still owns
wire deduplication. Long arrival gaps, suspended gameplay, non-moving owners,
overflow and level/connection resets discard pending journal entries at an
explicit cutoff. After a legacy player physics pass, the journal retires
records through the completed cursor and releases their storage. This is
bookkeeping under latest-command authority: one frame may retire multiple
records even though they were not each individually simulated. The PMove
consumer must replace that retirement rule before granting prediction.

The strict Linux `vkquake` build and a fresh private-profile loopback smoke
passed after the journal change: signon, settled movement, four consumed
shells and a bounded advancing ACK with prediction permission off. Queue
overflow/discontinuity and per-command PMove behavior still need focused
qualification; this is not a prediction or headset-parity claim.

## Server PMove owner: Astra senior disposition

A local `gpt-6-astra`/`max` review checked the current `2.0` server owner,
QSS-M's command callback path and the staged shared solver. Its key finding is
that suppressing native acceleration alone would still duplicate stock QuakeC
jump/water velocity changes. The choice of movement owner, command action pose,
QuakeC callbacks and completion is therefore one decision, not independent
switches.

| Recommendation | Disposition |
| --- | --- |
| Keep vkQuake's player slot, host loop, world physics and pusher ownership; run PMove only for explicitly selected private clients. | **Adopt.** A second server loop would duplicate working policy. |
| Run one PreThink/PostThink around a whole queued batch. | **Reject.** Distinct accepted attacks and impulses need their own command context and weapon pose. |
| Assume skipping `SV_ClientThink` prevents all duplicate movement. | **Reject.** Stock QuakeC PreThink also changes velocity. Reconcile only for an explicit stock-compatible path; generic mods must not lose legitimate velocity changes. |
| Treat 32 entries/250 ms of queued input as a simulation-rate limit. | **Reject.** Add a separate allowance funded by unpaused server time before activating per-command movement. Complete commands must not be silently truncated. |
| Copy the staged all-edicts server physent collector or silently stop at 64 entries. | **Reject.** Reuse vkQuake's area tree, include supported negative-skin collision volumes and fail explicitly if the shared PMove physent capacity is exceeded. |
| Enable prediction as soon as PMove runs. | **Reject.** The owner snapshot still has `pmovetype=0`; it needs a matching reset, movement settings, completed ACK and epoch before advertising replay. |
| Run an idle synthetic zero-duration QuakeC lifecycle when no commands are queued. | **Defer.** Idle callback behavior needs comparison with the pinned reference before selection. |

The next implementation proof is a default-off, stock-compatible walk path
that handles two ordered accepted commands with separate QuakeC action/pose
contexts, one bounded duration owner and one PMove collision owner. It must
preserve pusher/trigger/impact behavior, consume roomscale once, and retire only
after successful callbacks. Start by restoring the server physent collector as
a narrow area-tree adapter; it must report overflow rather than omit nearby
collision. This adapter does not authorize prediction or switch the live player
path by itself. Later live acceptance must include walking/jumping, two poses
with target damage, ordered impulses, blocked roomscale, triggers, elevators,
pause/reset and owner snapshot coherence. Delayed scheduled weapon Think pose
and the exact idle callback policy remain unresolved compatibility questions.

## Server physent collector checkpoint

The shared PMove world now has a callable server collector built over vkQuake's
existing area tree. It gathers linked nearby solids, preserves owner and point
entity exclusions from `SV_ClipToLinks`, maps supported contents skins and BSP
model identity, and reports invalid world/model data or the 64-physent overflow
as failure. It clears the partially gathered list on failure. Its caller must
supply conservative absolute query bounds and must never run PMove on failure.
This replaces neither the active server player path nor the client collector;
prediction remains disabled. The strict Linux `vkquake` target builds.

`SOLID_NOT` and trigger entities are not linked into the server's solid area
tree. The private snapshot writer now keeps them non-solid even when a negative
skin encodes contents, matching the server PMove collector. Predictive replay
still needs a broader server/client collision parity check.
The later player adapter also needs a deliberate response to collector overflow
on dense maps and to pusher/rider ignore context; silently dropping colliders
would sacrifice correctness for apparent performance.

## Server movement settings checkpoint

The shared PMove module now builds server `movevars_t` from vkQuake's existing
gravity, stop speed, max speed, acceleration, friction and edge-friction cvars,
with QSS-M server defaults for the remaining settings. It also exports the
same values into QSS-M movement stat slots for a future private snapshot
writer. The producer leaves the active client/server PMove globals untouched;
there is no call site or authority switch yet. QSS-M's server defaults enable
slidefix and bunny friction, while client fallbacks remain zero until a server
actually advertises those values. The strict Linux `vkquake` target builds.

The selected stock-QC trial now exports these values from `SV_CalcStats` through
vkQuake's existing replacement-stat delta writer. Its admission rejects custom
stats, avoiding collisions with movement slots 225–253 while the trial remains
stock-only. A Linux selected-peer loopback received valid movement flags,
gravity 800, maximum speed 320, jump speed 270 and step height 18; it also
retained movement, firing, advancing completed ACKs and prediction disabled.
Before admitting mods, resolve custom-stat slot ownership. Before enabling
prediction, resolve VR jump-speed policy at the selected command owner and
export exactly the speed used by the server step. Stat receipt alone is not
owner/ACK epoch association or predicted collision parity.

## PreThink room-scale adapter checkpoint

The shared PMove module now exposes a room-scale-only collision sweep for the
future server command owner. It reuses PMove's nudge, ground categorization and
step/slide path before QuakeC PreThink, without running ordinary locomotion or
Gorilla preparation. It preserves the solver's angle state and leaves zero
room-scale input alone. The caller still owns collecting physents, copying the
result back to the entity, relinking/touch dispatch, and clearing the room-scale
delta before its later `PM_PlayerMove`. There is no live call site or authority
switch yet. The strict Linux `vkquake` target builds.

## Rejected broad server-owner trial

A draft stock-QC, default-off WALK owner was removed before commit. It repeated
the queue head within a frame rather than advancing a local accepted-command
cursor, which would duplicate attacks and impulses. It also let
`PM_PlayerMove` clear touches from its new pre-PreThink room-scale sweep,
overwrote QuakeC velocity changes from impacts, and skipped the normal PreThink/PostThink
lifecycle whenever the accepted queue was empty. Hard-closing the selector while
retaining unused state was rejected as dead scaffolding. The existing private
latest-command physics path remains the only live server owner.

The next patch should be smaller: first make a single accepted command run from
the existing `SV_Physics_Client` slot, with one local queue cursor and completion
only after its callbacks. It must define the empty-queue callback path and fall
back before callbacks if physent collection fails. A second command in the
same host frame then proves ordering and time allowance. Keep the trial off and
prediction disabled until this behavior is observable against the legacy
reference; do not add another server loop or protocol layer.

## Single-command owner: Astra senior disposition

A local `gpt-6-astra`/`max` review checked the failed trial against the active
server paths and QSS-M donor. The reviewer corrected the room-scale premise:
`SV_ApplyPrivateRoomScaleMove` deliberately disables impact callbacks, so a
new pre-PreThink PMove touch dispatcher is not needed for reference parity.
The stock `progs.dat` was independently found in the configured `id1/pak0.pak`
and its 340014-byte MD4-XOR hash was verified as `cf69c3e2`.

| Recommendation | Disposition |
| --- | --- |
| Select the owner before `SV_ClientThink`, stage immutable command input, and keep one `SV_Physics_Client` lifecycle. | **Adopt.** Selecting inside physics after native acceleration would double movement. |
| Transplant QSS-M's receipt-time command owner. | **Reject.** Its `usingpmove` branch correctly skips native acceleration, but moving callbacks into receipt would duplicate vkQuake's host/physics ownership. |
| Dispatch touches from the new PMove room-scale helper. | **Reject for the first proof.** Reuse the native no-callback room-scale sweep, clear its delta before ordinary PMove, and preserve the current trigger/impact policy. The helper remains available for a later deliberate solver change. |
| Retire the queue head on receipt or run legacy physics when no new command fits the time allowance. | **Reject.** Keep an owner-local cursor and retire only successful commands after callbacks. Idle/insufficient-credit frames need a maintenance QuakeC lifecycle under the selected owner; its exact button/weapon cadence must be qualified. |
| Enable prediction with the first executed command. | **Reject.** Owner snapshots, ACK association, movement settings and reset epoch remain incomplete. |

The next observable proof is a restricted, default-off stock-QC dry-WALK owner
running one accepted command while a different second command remains queued.
Use a remote connection to avoid the current single-player synthetic ACK. Verify
the first command's movement, weapon pose/damage and completed ACK, and absence
of effects from the second; then check duplicate receipt, insufficient credit,
idle weapon Think and once-only blocked room-scale translation. Keep pusher,
water, custom-physics and Gorilla states under the legacy owner until they have
their own parity evidence. A server-owner failure after activation must be an
explicit error/discontinuity, never a silent whole-frame legacy replay.

## Default-off stock-QC WALK trial checkpoint

`sv_private_pmove_walk 1` now selects a remote, pinned-private, stock-progs,
dry-WALK owner at `begin`. Selection is latched for that client and never
silently falls back to native acceleration. It stages received commands in the
existing private queue, grants at most 250 ms of command-time credit, and runs
at most one complete command in each server frame. An idle or insufficient-credit
frame runs a zero-time QuakeC maintenance pass using only the last completed
levels and pose. The command path reuses the existing native room-scale sweep
before PreThink, then the shared PMove solver for ordinary movement; it keeps
prediction disabled and retires only after callbacks complete. The selector
defaults off and is not archived.

On this Linux host, the exact rebuilt binary passed a dedicated-server/desktop
loopback smoke on stock `e1m1` with a 100 Hz server tick and 72 FPS client:
the server reported trial selection, authoritative movement advanced about
267 units, shells decreased from 25 to 21, and move ACKs advanced from 69 to
285 without prediction permission. The same rebuilt binary passed the
default-off private-peer smoke with the ordinary 40 Hz server tick and 144 FPS
client. Both checks used disposable profiles linked to the configured Straight
game data. These are movement/firing/ACK smoke checks, not proof of weapon-pose
damage, queued-command action isolation, VR room-scale, or jump/trigger parity.

The selected path still rejects water, pusher contact, custom physics, Gorilla
input, pause, and unsupported owner changes by disconnecting rather than
replaying legacy movement after an action. In the first one-command-per-frame
trial, 144 FPS client input overloaded a default 40 Hz server and hit the bounded
queue; the later batching checkpoint below addresses this narrow rate mismatch.
Near a stock-map door, a broader pusher-proximity gate disconnected at spawn;
the current gate checks actual PMove pusher contact instead. QuakeC callback
and weapon timing still need qualification before this selector can become a
general private owner or prediction can be enabled.

## Senior review of the selected owner

A local Astra `max` read-only review identified two high-priority defects and
one weapon timing defect in the first trial commit. The insufficient-credit
maintenance branch copied the queued head before deciding not to run it, so
queued buttons and pose could reach QuakeC early. It now explicitly selects
the last completed command, or neutral input before the first completion.
Weapon Think was also scheduled against packet duration while `qcvm->time`
remained on the host clock; its eligibility and QuakeC frametime now use the
saved host frame in both command and maintenance passes.

At review time, the command-rate defect remained: one complete command per host
frame could not sustain a faster sender, and a larger queue would only delay
disconnection. The selected path was an explicitly restricted proof, not the
release owner. The batching checkpoint below follows the review's recommendation
to process bounded credited commands with a local queue cursor and once-only
callbacks. Reuse the native player lifecycle where possible;
the current command and maintenance copies should be consolidated rather than
grown into parallel policy. The review did not establish jump hold/release,
weapon target damage and delayed pose, packet-loss idle behavior, non-world
ground, or impact callbacks that mutate collision state.

## Command-rate architecture decision

The next selected-owner increment should consume a bounded sequence of whole
queued commands in the existing `SV_Physics_Client` slot. A local offset walks
the immutable queue; `SV_FinishPrivateUsercmds` still retires only through the
last successfully completed sequence after server physics. Add host-frame
credit once, cap both the number of commands (initially eight) and accumulated
time, and run the zero-time maintenance lifecycle only if no command completes.
This is the smallest adapter over the current queue and PMove owner. Moving
QuakeC actions back to packet receipt, introducing another physics loop, or
coalescing commands would duplicate ownership or lose per-command pose, impulse
and button edges. Merely enlarging the queue does not solve a sustained rate
mismatch.

The selected stock-QC dry-WALK proof keeps `qcvm->time` on the vkQuake host
clock throughout one physics frame; `SV_Physics` advances it once after all
entities. This matches QSS-M's independent command path, which can execute
multiple received commands while the server frame time is unchanged. Movement
and the native room-scale sweep use each command's duration; scheduled weapon
Think retains host-frame eligibility. Stock dry `PlayerPreThink` has no
`frametime`-scaled movement rule; its water drag is outside this trial. The
release owner still needs a separate review of mods, water, death, pushers and
the exact temporal behavior of weapon actions before widening admission.

The first observable batching proof is a stock-map remote peer at the default
40 Hz dedicated tick and a 144 FPS client: selected authority stays connected,
movement/fire and bounded ACKs advance, and no queued action appears before its
own completion. The existing 100 Hz/72 FPS proof and default-off legacy smoke
remain references; a passing build or a larger queue alone is insufficient.

## Bounded command batching checkpoint

The selected owner now adds host-frame credit once and walks up to eight queued
commands through the existing client physics slot, preserving each command's
input and pose. It records completion after each successful callback sequence;
the existing end-of-physics queue retirement then removes only completed heads.
If no command fits, it runs one maintenance lifecycle, while a frame that has
completed a command does not run an extra maintenance pass. PMove's jump debounce
is now stored in `client_t` alongside the other selected-owner state instead of
a separate static slot table.

The exact rebuilt Linux binary passed the previously failing stock `e1m1`
loopback: a default 40 Hz dedicated server explicitly selected the trial with
a 144 FPS private desktop client. The client stayed connected through the
movement/firing interval; displacement was about 267 units, shells fell from
25 to 21, and completed ACK advanced from 69 to 283 of 286 sent. Prediction
remained disabled. This demonstrates that the sampled rate no longer saturates
the 250 ms queue in this route. It does not yet prove per-command VR pose/damage,
action isolation on deliberately held queue heads, complex collision parity,
or arbitrary jitter/packet-loss behavior. Those remain release gates, as does
the wider QuakeC clock and unsupported-state review.

A focused repeat of the same selected peer run observed the first attack
command at sequence 72 and the first authoritative shell decrease with a
completed ACK of 73. The visible ammo effect therefore did not precede its
attack command's ACK in that run. This narrows the action-ordering gap but does
not cover impulses, rapid alternating poses, or a deliberately credit-starved
queue head.

## Astra batching review disposition

The local Astra senior review conditionally accepted the eight-command cursor
for the restricted, default-off trial. It found no actionable defect in the
queue indexing, once-per-frame credit, completed-only retirement, idle gating,
or move of jump debounce into `client_t`. It also found no need for a separate
world clock or receipt-time movement owner based on the current evidence.

| Review point | Disposition |
| --- | --- |
| Preserve the bounded cursor and host clock | **Adopt.** Stock dry-WALK batching follows the existing physics slot and QSS-M's same-frame command precedent. |
| Enlarge the queue or move movement to packet receipt | **Reject.** Neither is required to fix sustained rate mismatch, and receipt-time execution would duplicate ownership. |
| Consolidate copied native/maintenance/command lifecycles | **Adapt later.** Delete only demonstrably identical sequences; do not add a new state framework. |
| Optimize repeated PMove copies and physent collection immediately | **Defer.** Up to sixteen collections per selected client frame are possible, but no representative frame cost has been measured; callback mutations require a fresh post-QC list. |

This acceptance is not a parity claim. Delayed weapon damage and pose, held
queue-head actions, jump/idle gaps, non-world ground, triggers, packet loss,
unsupported-state recovery, and representative CPU cost remain unverified.

## Astra owner/settings/ACK review disposition

A local `gpt-6-astra`/`max` review checked the post-stat-export private owner
path. The selected Linux loopback receives valid movement stats, but the
replacement writer sends stat deltas only in the first unreliable datagram of
a snapshot burst. A later owner ACK can arrive with stale yet valid
settings if a changed-settings packet is lost. The client now rejects absent
movement stats, but that guard does not solve stale settings. Prediction remains
disabled.

| Recommendation | Disposition |
| --- | --- |
| Treat owner reset, completed ACK and complete movement settings as one eligibility proof, using the existing codecs. | **Adopt for the next vertical slice.** Repeat the full movement-stat set and a baseline-relative selected owner reset in every selected-private entity datagram, including continuations. Reserve room for the mandatory group before optional entities; fail boundedly if it cannot fit. Leave public snapshots untouched. Measure the bandwidth before choosing a cached/versioned alternative. |
| Account for ownerless continuations invalidating the client's candidate snapshot. | **Adopt.** Keep the existing conservative invalidation and require each selected continuation to re-establish eligibility at the message-end boundary. Track complete movement-stat receipt per message, including zero-valued settings. |
| Keep semantic discontinuity separate from decoding resets and replay completeness. | **Adopt.** `UF_RESET` is a decoding baseline, not a semantic transition. Keep prediction permission off while proving packet coherence and while the server's persisted jump timer is not represented in the client replay seed. |
| Exercise changed settings, split/lost packets, pause and recovery. | **Adapt.** Test changed and zero-valued settings, split/lost packets, complete burst and following-frame eligibility. Selected clients currently disconnect when input is cleared on pause; seamless pause recovery would be a separate behavior expansion. |

This is a narrow adaptation of vkQuake's replacement stat/entity writer and
client end-of-message candidate gate, not a second protocol or snapshot owner.
The immediate proof must preserve completed-only ACK retirement and keep the
prediction flag false. Later permission requires semantic discontinuity
handling, jump debounce parity, collision/VR command replay and broader
gameplay validation; repeated bytes alone do not authorize it.

## Selected-private snapshot coherence checkpoint

The selected stock-QC sender now uses the existing stat opcodes to put all 20
movement settings, including unchanged and zero values, ahead of each entity
update datagram. It skips those slots in the ordinary stat-delta loop. Each
selected datagram also carries the completed ACK and a baseline-relative owner
reset; optional entities follow, with the world's pending removal emitted
first. The sender reserves room for the mandatory owner group and disconnects
the selected trial if it cannot fit or an optional continuation cannot make
progress. Public and default-off peer writers retain their prior path.

The client accepts a replay candidate only when the same complete server
message carried every movement stat, an accepted completed ACK and a matching
reset-decoded owner. A later movement-stat update invalidates the earlier
candidate. This reuses the existing parser and message-end commit point; the
private prediction permission and owner `pmovetype` remain disabled.

The strict Linux build, sanitizer-backed owner parser fixture and whole-message
GDB probe pass. A selected `e1m1` dedicated-server loopback moved about 267
units, fired four shells, received valid stock movement settings and a matching
owner/ACK candidate (ACK 284), while prediction stayed off. A fresh public
loopback moved/fired normally and retained zero private movement stats. The
parser probe also rejects an owner update without that message's complete stat
group. Packet-loss recovery, changed movement cvars, VR poses, jump debounce
and semantic teleport resets still need proof before
prediction can be enabled. The repeated group adds 120 bytes per selected
datagram before the owner update; measure its traffic and frame cost on large
maps before optimizing it.

The implementation received a second local `gpt-6-astra`/`max` code review.
It found no confirmed P0/P1 defect, but caught two continuation-specific
behavior regressions before commit:

| Finding | Disposition |
| --- | --- |
| Repeating an identical owner reset at the same snapshot time forces vkQuake's interpolation endpoints to collapse. | **Fixed.** The private parser still decodes the owner and establishes the ACK candidate, but leaves presentation history intact if origin and angles are unchanged. Changed owner state at frozen server time still follows the existing processing path. The owner fixture covers an identical same-time repeat. |
| Failing when no optional entity fits in the first packet can disconnect a peer even though a clean continuation would fit it. | **Fixed.** The no-progress failure now applies only to a clean continuation. The first packet can carry its mandatory group and retry optional entities after dropping its ordinary stat/damage prefix. |

The `e1m1` low-budget probes did not create an actual continuation: only four
entities were visible in the sampled stock spawn. A controlled `start` run then
limited the selected peer to 220-byte datagrams and marked its five visible
non-owner entities for one reset. The production writer entered a continuation
at `snapshotresume=34`. The Linux client remained connected, moved about 654
units, consumed four shells and ended with complete stock movement settings and
a valid owner candidate matching completed ACK 285; prediction stayed off.
The reproducible GDB wrapper is `tests/private_selected_split_server.gdb`.
This proves one real split path, not visual interpolation quality, general
large-map behavior, resend under packet loss, or changing server movement
settings. Those remain acceptance gates.
