# Coupled movement and private protocol migration

The next P1 proof is a migrated client playing a real map against product
`1327f795cc2e3a8e4f7c9d68e31d64383930cc00` on a dedicated server, with prediction,
physical roomscale, tracked hand aim and authoritative firing. A local map is
insufficient: the product's `SV_PMovePolicyEvaluate` excludes local single-player.
Windows/ARM remain deferred; this proof can use two local Linux processes.

## Verified incompatibilities and minimal adaptation

The destination retains vkQuake's public NetQuake/FitzQuake/RMQ negotiation and
single-command transport. Product `protocol.h:53` defines a private extension
set and uses bit `0x80` for explicit command milliseconds. Public QSS-M uses that
bit for INFOBLOBS. Product `SV_SendServerinfo` forces its extension set without
negotiation, and `CL_ParseServerInfo` requires it. Private movement always adds
an extension byte, redundant command history, separate hand aim/muzzle data and
roomscale displacement. Movement orientation is not the same as hand aim.
Copying the payload into ordinary donor PREDINFO traffic would misframe peers.

The minimal design keeps the existing command, transport, VM and world owners.
It ports the product's shared PMove algorithm and adapts its hull calls to donor
`SV_RecursiveHullCheck`, retaining the donor's contents-mask contract. It does
not replace world collision, create a local-only VR gameplay authority or add
another command queue beside the intended inherited queue. Source Gorilla and
roomscale algorithms remain part of the one solver; unavailable producers are
not advertised as supported.

The final dialect design uses one private marker/version in existing `pext`
key/value negotiation, echoed before dialect-sensitive extension fields in the
existing serverinfo prefix. It is a selected version, not a bitmask intersection.
Private command, ACK, stat and solid decoding require explicit dialect admission
and the supported version's RMQ/extension prerequisites. An FTE bit alone cannot
identify the private dialect. No additional handshake service is needed.

For the pinned server, a connection-scoped explicit legacy opt-in selects its
known layout and survives map-level `CL_ClearState`. It cannot silently convert
an ordinary public connection into a private one. This direction is **2.0 client
to pinned server**; an unchanged old client joining a new explicitly negotiated
server is a separate compatibility requirement and cannot be claimed from it.

These admission rules are a reviewed implementation plan, not an enabled wire
feature. Do not advertise a private version until its full accepted layout is
implemented. No serializer or capability advertisement is changed by groundwork.

## Review disposition

Astra (`gpt-6-astra`, explicit/effective `xhigh`) verified the source and donor
owners before reviewing. Main spot-checked the negotiation pair parser/writer,
network catch-up loop, signon-baseline owner snapshot and single-player policy.

| Recommendation | Disposition |
|---|---|
| Reuse `Cmd_ForwardToServer`, `SV_Pext_f` and serverinfo prefixes; avoid another negotiation layer. | **Adopted.** One private version pair and connection-owned selected dialect, with explicit old-server opt-in. Unknown versions never partially activate private parsing. |
| Separate accepted commands, completed simulation ACKs, transport ACKs and Gorilla state sequence. | **Adopted.** Port their source semantics, queue overflow/discontinuity handling and mode epochs together. Prediction requires matching recoverable authoritative owner state. |
| Owner snapshots must recover independently of lost preceding unreliable updates. | **Adopted.** Reuse `SVFTE_BuildMoveSnapshot`'s signon-baseline reset and its refusal to authorize prediction when the native owner is absent. |
| Donor catch-up iterations can manufacture minimum-duration commands if the monotonic sampler is dropped in unchanged. | **Adopted as a required pacing check.** Integrate private sampling with the source's paced network pass while preserving public donor behavior. This risk is inferred from verified loop differences; it is not a measured failure yet. |
| Roomscale is once-per-command displacement, including across PM substeps. | **Adopted.** Reuse the source solver and command ownership. Do not turn displacement into velocity or replay it once per substep. |
| Freeze/death/teleport, QC fallback and event-pose ownership are required at first activation. | **Adopted.** Frozen movement consumes tracking without displacement; fire uses its originating command pose. Preserve legacy once-per-physics-frame QC hooks and command-driven callbacks as distinct source cases. |
| Copy reusable PM code without dummy gameplay implementations or premature capability registration. | **Adopted.** Stage the solver and exact boundary declarations until real client/server owners are connected. Definitions or compiling packets alone do not establish support. |
| Qualify the donor hull adapter rather than replacing collision. | **Adopted.** Cover ordinary movement and stationary/startsolid, rotated brush, box and contents cases through actual donor collision. Exact reference parity remains an acceptance task. |
| A broad player-solid leaf mask changes raw `CONTENTS_CLIP` behavior only in the optimized donor tracer. | **Adopted correction.** All three adapter calls pass only the `CONTENTS_SOLID` leaf bit, matching the source checker. Existing physent contents filtering remains separate. A raw-clip regression passes both donor trace paths. |
| The inherited server-gather branch converts an edict before checking the active VM. | **Adopted.** Validate the required active VM before `NUM_FOR_EDICT`; remove the misleading later null guard. World gathering remains unqualified by this fixture. |
| Tolerance comparisons can silently accept NaN. | **Adopted.** The test helper explicitly rejects nonfinite actual and expected values. |
| Prove the new client against the pinned dedicated server before implementing both new endpoints simultaneously. | **Adopted.** It provides a fixed behavioral peer and exposes client/prediction failures independently of the new server. |

## Implementation sequence and activation gate

1. Reuse PMove/Gorilla types and solver, preserving donor command accumulators
   and integer fields. Compile the staged object and exercise donor collision.
   Keep the gameplay executable on its existing movement path.
2. Implement connection-owned admission and the production legacy adapter.
   Port client command duration/redundancy, solid/stat decoding, authoritative
   ACK baseline, movement variables and replay as one activation milestone.
   Preserve the source movement/QC policy; never infer support from declarations.
3. Connect inherited hand/weapon/movement producers to current OpenXR poses,
   including physical hand identity, invalid tracking, worldscale, turning,
   body-relative muzzle and roomscale authority. Prove the dedicated-peer case.
4. Port the new server queue, QC/physics ownership and snapshot producer using
   that same command representation. Check new/new private peers and ordinary
   donor peers, then independently resolve old-client/new-server admission.
5. Expand remaining modes and feature-map rows through their existing owners.

Required acceptance includes loss, duplication, sequence wrap, split-owner loss,
hitches, frozen tracking/thaw, death, teleport and QC fallback. Compare actual
movement/fire against the reference. Unit collision tests, mock packets and
successful compilation are groundwork evidence only. The migration goal and P1
remain open until the requested behavior is implemented and verified.

## Groundwork implemented and checked

Terra ported `pmove.c`, `pmove.h` and the three Gorilla headers in an exclusive
write scope; main reviewed the source diff and integrated shared command/stat,
QC-global and world/server declarations. The Gorilla headers are unchanged
copies. PMove changes are limited to the hull signature/mask, donor VM world
ownership and replacing the broad VR header dependency with a session-active
view query. A paused or unavailable pose does not switch the jump-default policy
to desktop; enabled-session state and usable-view state remain distinct.

The standalone object compiles against current headers with bootstrap build
flags. The solver is deliberately absent from engine source lists while its
actual authority owners remain unported. No missing server function is replaced
by a stub. The existing executable also builds after shared-type changes.

The focused production-solver fixture passes walking, floor contact, jumping,
freeze, once-per-command roomscale across four substeps, invalid displacement,
box collision, rotated brush hit/normal, stationary/startsolid, water and raw-clip tests.
It explicitly selects both the donor slow and optimized hull implementations.
The test initially relied on cvar initializer strings, which leave numeric values
zero without registration; main corrected it to set the trace selector explicitly
before counting optimized-path coverage. Test cvar/diagnostic stand-ins and
constructed hulls do not prove a networked game, full collision equivalence or
runtime controller behavior. See `tests/README.md` for the reproducible command.

Astra's final code review found the raw-clip mismatch and the inherited null-VM
check ordering described above. Main corrected both and hardened the test's
finite-value checks, then reran the staged object and solver fixture successfully.
The standalone checks do not cover world gathering, VM ownership, movement-stat
import, cached jump defaults, Gorilla locomotion or swimming; retaining those
algorithms does not establish their destination runtime parity.
