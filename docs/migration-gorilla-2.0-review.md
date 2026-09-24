# Gorilla locomotion integration: verified design brief

Status: Astra senior review complete; implementation underway. This is a solo-operator engine port;
the aim is the smallest end-to-end adapter, not a new movement architecture.

## Goal and behavioral reference

Port the inherited opt-in Gorilla hand locomotion (including wall/platform
contacts, swimming, server permission, and predicted motion) to the vkQuake
based OpenXR client. Desktop movement and ordinary VR stick/room-scale behavior
must keep their current owners. Reference: the pinned QuakeSpasm OpenVR
`Quake/vr.c:4013,7395`, `Quake/vr_gorilla.h`, `Quake/vr_gorilla_swim.h`,
`Quake/pmove.c:537,2014`, and `Quake/sv_user.c` Gorilla authority paths.

## Evidence and environment facts

| Claim | Status and how to verify |
| --- | --- |
| Branch and worktree | [verified: `git status --short --branch`] `2.0` in `quakespasm-2.0`; `docs/migration-2.0.md` is user-dirty and must not be edited/staged. `main` must remain untouched. |
| Shared solver already present | [verified: `rg 'VRG_Step' Quake/pmove.c`; inspect `Quake/vr_gorilla.h`, `vr_gorilla_swim.h`, `vr_gorilla_types.h`] The fork's pure hand trace, anchor, launch and swim helpers are already present. `pmove.c:1936–2032` can consume raw or authored motion and emit authored motion. Re-copying the solver would duplicate policy. |
| Prediction and wire substrate present | [verified: `Quake/pmove.h:91–99`, `cl_main.c:1163–1247`, `cl_input.c:620–732`, `sv_user.c:729–812`, `cl_parse.c:2795–2928`] The command structures, raw/trusted encoding, decode, replay gates and ACK state parser exist. These are not evidence that Gorilla can be enabled. |
| Producer and negotiation missing at review | [verified against the pre-`56f38f80` tree] The opt-in `vr_gorilla` preference existed, but no OpenXR hand sample filled the command and no server permission offer was available. See the implementation checkpoint below for the newer state. |
| Server authority blocked | [verified: `sv_main.c:683–724`, `sv_user.c:1073–1081`, `sv_phys.c:4485–4504,4863–4866`, `server.h:166–325`] The current stock-only private WALK trial explicitly rejects Gorilla, and its PMove invocation sets `gorilla_allowed=false`. `client_t` has contact state but no Gorilla authority/queue state. Ordinary nontrial physics still exists; there is no completed Gorilla state owner there. |
| Trial is not the default server path | [verified: `sv_main.c:43,678–731`] `sv_private_pmove_walk` defaults to `0`; when enabled, admission requires a remote spawned owner, exact stock progs identity, dry WALK, no custom stats/physics or pusher. Treating that trial as the sole Gorilla owner would fail the inherited default-on server permission and local/legacy/mod paths. |
| Donor's tracking shape | [verified: `../quakespasm-openvr/Quake/vr.c:7395–7426`] `VR_GetGorillaSample` needs both tracked hands, head, body origin, a reset latch, and velocity in Quake units; it suppresses sampling in menus/adjustment. It uses an opt-in cvar and a server permission gate. |
| OpenXR input frame | [verified: `Quake/vr_input.c:3410–3500`] Current pending command preparation has the active frame, dominant hand, calibrated hand/room-scale data and session/focus guards. The raw frame's physical left/right slots can supply both palms; conversion to body-relative Quake coordinates must be checked at this seam. |
| Hand velocity availability | [verified: `Quake/vr_openxr.h:21–25`, `vr_openxr.cpp:272–289`, `vr_input.c:1434–1503`] Each OpenXR hand velocity has an explicit validity flag. The existing contact path fails closed when velocity is unavailable; Gorilla raw samples must not silently treat missing velocity as a stationary hand. |
| Runtime/platform limits | [unverified] No headset or Windows/ARM qualification is requested now. Linux source/build/fixture verification is available. Do not infer physical hand velocity quality from action-profile declarations. |

## Proposed minimal adapter and open decisions

1. **Sample owner.** Lean: add the opt-in cvar/reset latch and one two-hand
   producer in `vr_input.c`, called during existing pending-command preparation
   (not each rendered eye or send retransmission). Reuse `view.c` body/yaw/unit
   conversion and the frame's tracked physical hands. Considered a new VR
   locomotion manager; reject because `vr_input.c` and PMove already own the
   sample and solver. Open: how to bind posture/floor to the existing view
   anchor so map changes and re-centers do not inject motion.
2. **Permission and mode.** Lean: port donor's negotiated raw/trusted policy
   at the current private command/ACK owner, with no public dialect changes.
   Avoid declaring support merely because codec fields exist. Considered
   always enabling raw mode locally; reject because server authorization and
   matched replay are required. The first vertical proof must use default
   ordinary physics; the stock WALK trial is not a compatible sole owner.
3. **Authority and prediction.** Lean: reuse `VRG_Step` inside the existing
   PMove/QC boundary and the current command journal. Server owns accepted
   state sequence and permission; client seeds replay from ACK state or trusted
   generation. Considered a second server movement pass or client-only hand
   displacement; reject because either duplicates movement or diverges under
   loss. Open: how to preserve QuakeC PreThink/PostThink and native velocity,
   and how to extend beyond the dry stock WALK trial to water, ladders,
   pushers, custom physics and frozen clients without silently changing their
   existing behavior.

The permission, server authority and replay questions are likely one decision:
the first admitted mode determines what state can be predicted. Merging these
into one design is in scope for the reviewer.

## Smallest end-to-end proof and stop conditions

An opt-in OpenXR two-hand sample must produce one private command, pass an
explicit server offer, move the unchanged player hull against a wall, and
return a state/ACK that replays to the same position. A desktop client on the
same server remains ordinary. Reset on focus loss/recenter/map transition;
packet duplication and late ACK must not replay a hand stroke. Then extend
to moving supports, swimming, ladder/native-QC cases and user presentation.
Source presence, packet roundtrips and mocked callbacks alone do not close
this proof. Reopen the design if implementation duplicates a movement state
machine, adds a second command clock, or repeatedly patches interactions
among new layers.

## Review request

Verify each load-bearing source claim first. Re-rank/merge/kill the three
decisions, challenge the proposed first vertical proof, identify the most
dangerous correctness and performance regressions, and recommend the smallest
implementation sequence. Depth budget: focus on sample-to-authority-to-replay
and QC interaction; no general OpenXR, Vulkan, foveation, asset, or unrelated
networking review. Distinguish implementation decisions from any genuinely
human preference. Return a prioritized critique and concrete source anchors;
do not edit code.

## Astra senior review disposition

The review found that the main architecture choice is the **existing movement
owner and QuakeC cadence**, rather than raw versus trusted representation.
Donor trusted motion is restricted to compatible native PMove with a matching
generation; local, legacy and custom-QC paths use bounded raw samples. Keep
client preference off, server permission on and server trusted policy on by
default, as in the donor. Capability alone must not activate movement.

| Recommendation | Disposition and reason |
| --- | --- |
| Jointly specify permission, authority and replay | **Adopt.** The ACK and predicted state must identify the movement owner and the accepted command. Raw/trusted encoding remains a separate choice. |
| Make the stock WALK trial the Gorilla authority | **Reject.** `sv_private_pmove_walk` defaults off and its remote, exact-stock, dry-WALK admission excludes required local, mod and water paths (`sv_main.c:43,683–724`). |
| Import the donor's auxiliary queue wholesale | **Adapt.** The target already has a sequenced command FIFO (`sv_user.c:956–1015`). Add bounded once-only sample consumption and ordered OFF/reset handling inside the ordinary physics owner; avoid a second clock or movement pass. |
| Recopy the Gorilla solver | **Reject.** The three shared solver headers are byte-identical to the donor and `pmove.c:1936–2032` already supports raw and authored motion. |
| Commit hand-controller state while preparing preview commands | **Reject.** `VR_InputApplyPending` is used for preview. Copy candidate input there, then consume a pending reset and author any trusted contribution once at final sequence/duration assignment in `CL_SendPrivateMove` (`cl_input.c:943–982`). Retransmit stored commands; use a disposable state for preview. |

The ordinary server path must consume every accepted sample once, including a
stroke followed by OFF in the same packet bundle. It must preserve QuakeC's
legacy once-per-frame think cadence and QC-written angles/buttons/duration.
Recheck eligibility after callbacks, and invalidate contacts when the owner
dies, teleports or changes. The donor's `gorilla_prepared` guard avoids double
motion when QC delegates into PMove. The target's stock trial has a different
command-specific QC lifecycle (`sv_phys.c:5054`), so simply allowing Gorilla
there would change gameplay behavior.

The server currently parses but does not emit Gorilla state or trusted
generation in movement ACKs (`sv_main.c:1657–1680`). Extend the existing
snapshot contract for supported owners. Raw state must be tied to its ACK
sequence; trusted motion must match the server generation. Stale trusted
motion must never silently fall back to raw, and ordinary packet gaps must not
fence every trusted stroke. Complete repeated owner baselines currently exist
only for the trial and need careful extension.

Implementation order: finish negotiated activation and physical two-palm
sampling; integrate raw samples into default ordinary physics using the target
FIFO and native collision; prove a real-BSP wall stroke through producer,
sender, server and returned ACK with `sv_private_pmove_walk=0`, including local
play, stroke/OFF bundles, loss, duplicates and an ordinary peer; then complete
mod/QC lifecycle adapters and trusted authoring/replay. Compare position and
velocity at matching sequences before claiming prediction parity. Clearing
`FL_ONGROUND` alone may leave retained pusher support active and WALK can zero
launch velocity (`sv_phys.c:1837,2876`); verify that transition early.

View-anchor conversion, headset behavior and out-of-scope QC builtins remain
unverified. The legacy proof does not establish native PMove replay parity.
Reopen this architecture decision if a second movement state machine, new
command clock or repeated cross-layer repairs become necessary. No human
choice is needed for the stated parity goal.

## Implementation checkpoint after review

Commit `56f38f80` connects a bounded two-palm OpenXR sample to the existing
private command journal, independently of weapon calibration. Preview copies
the pending reset without consuming it; the final sequenced command commits
the latch. The server advertises raw protocol-1 permission on the reliable
stream only to spawned pinned peers, and accepts an exact capability reply.
`sv_gorilla` defaults on and `sv_gorilla_trustclient` is registered with the
donor's default, but trusted mode is **not advertised or active**. The optional
stock WALK trial advertises Gorilla as disallowed while it still rejects such
commands. Linux debug compilation passed.

This is not yet functional Gorilla movement: ordinary server physics still
does not consume the accepted raw samples, and no matching Gorilla state is
written in ACKs. The next implementation must reuse the existing private
command FIFO and native collision/QuakeC frame owner, including ordered OFF
boundaries, state invalidation and coherent client replay.
