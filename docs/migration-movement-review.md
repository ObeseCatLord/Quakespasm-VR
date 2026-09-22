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

## Command-body and snapshot-bound integration

The next staged component reuses the pinned command-body writer and reader in
`cl_input.c` and `sv_user.c`. Their arguments provide protocol angle flags and
negotiated Gorilla capabilities; they do not own a connection, command queue,
clock or capability state. The pinned layout always carries explicit command
milliseconds and replacement-delta entity encoding. Public serializers and
capability masks remain their existing implementations.

The private model ceiling is explicitly 4096 where the pinned trusted-motion
reader applies it; donor `MAX_MODELS` is 8192. Substituting the donor capacity
there would change the pinned reader's accepted wire contract. Receipt time stays
with command acceptance and is never provided by the packet-body decoder.

The delta parser now retains decoded `solidsize`, which vkQuake previously read
and discarded. Public packets retain their existing 16-bit packed encoding.
Private tags 0/1/2/3/16/32 use the inherited meanings, selected only through the
new explicit `cl.protocol_qsvr` layout field. It is zero after client clear;
no production path currently sets it. Neither a matching FTE mask nor codec
availability constitutes admission. Unknown selected layouts and tags fail.

Retaining public collision bounds is an intentional behavior correction: client
world traces and the migrated PM/weapon-query consumers need those received
bounds. A production delta-decoder fixture checks both encodings, following
message alignment, retained old bounds, invalid/truncated tags, and a colliding
FTE mask without explicit private selection. It also passes decoded bounds to
the inherited weapon query and actual donor hull tracing against a synthetic
entity box. This is a parser-to-collision check, not a networked movement proof.

At this codec checkpoint, private activation still required connection admission,
duration sampling and pacing, command history/redundancy, complete authoritative
ACK/snapshot parsing, replay and the inherited movement/weapon producers. The
later staged transport checkpoint below records partial progress. No new
`connect` mode is exposed that would claim this unfinished support.

### Command codec review disposition

| Finding | Disposition |
| --- | --- |
| Pinned body field order, dependency flags, raw/trusted handling and 4096-model ceiling | Retained; Astra compared both production bodies with the pinned source and found no additional mismatch. |
| Nonfinite diagnostic time, float angles and QC cursor vectors were accepted by the pin | Adopted decoder rejection as malformed-input hardening; no new finite-value ranges or normalization. Command acceptance cannot substitute because explicit-msec normalization bypasses timestamp handling. |
| Float primitive could access bytes beyond a truncated message | Added bounds checking to the existing shared reader; finite-value policy stays in the private decoder. |
| Packed solid left shift used signed arithmetic | Cast before shifting and retain unsigned packed state. The sanitized parser-to-collision fixture passes. |
| Packed zero was expanded into nonzero bounds | Corrected zero to `ES_SOLID_NOT`, matching the pinned and QSS-M writer contracts. Added decode and actual no-collision checks for public zero and private tag-16 zero. |
| Network collision upper Z subtracted its bias as unsigned | Adopted a signed cast before subtraction in the existing donor collision owner; added above/inside negative-top box traces. |
| Legacy hull hits had no contents metadata and were discarded by the network-entity contents filter | Adapted only the network collision caller to label actual slow-checker solid hits before its existing filter/remap. Generic hull and server query behavior is unchanged. Both trace modes remain required by the local fixture. |
| Removed replacement entities retained bounds | Reused the pinned removal-state reset for both individual and full removals, including full-reset trace-cache invalidation. The fixture executes the production removal parser at unchanged snapshot time, then checks the actual weapon query. |
| Public collision bounds were discarded | Preserve the received state; explicitly record the resulting client-trace behavior change. Do not infer private admission from public FTE bits. |

The malformed-input corrections intentionally tighten the pinned reader;
valid finite command layouts remain unchanged. The prepared-command writer
continues to rely on its producer for valid data. No extra command queue,
transport, parser framework or simulation authority was introduced.

### Local verification for this checkpoint

The Linux vkQuake executable builds successfully after all codec and collision
changes. Both production-code fixtures pass with AddressSanitizer and
UndefinedBehaviorSanitizer. The codec fixture covers independent header/entity
bytes, complete optional payloads, capability/model limits, finite-value checks
and every physically truncated prefix of raw/trusted commands (including float
angles). The collision fixture covers decoder framing, non-solid transitions,
individual/full removal at unchanged snapshot time and negative-top bounds under
both donor trace implementations. The latter initially failed on the slow
checker's missing contents metadata; the caller-local correction resolves it.

These remain focused checks with constructed geometry and packets. A live
private connection and the pinned dedicated-peer movement/fire proof have not
been run for this checkpoint. Windows, ARM and headset qualification remain
separate deferred gates.

Astra's final bounded review found no remaining actionable findings after these
corrections. It verified the source/fixture logic and independently checked the
three golden command layouts against the pinned source; build and sanitizer
execution evidence comes from main/Terra. Approval covers the staged groundwork,
not private gameplay activation.

## Staged transport and ACK checkpoint (2026-09-22)

The existing `cl_input.c` owner now contains the pinned duration sampler,
current-plus-two command history and bounded transport ACK writer/flush. The
existing `cl_parse.c` owner receives extended movement ACK metadata and Gorilla
state only when `cl.protocol_qsvr == QSVR_PROTOCOL_PINNED`. The extended payload
is mandatory for that selection; PEXT bits cannot admit or downgrade it.
Full-payload validation precedes accepted-baseline changes. Stale ACKs cannot
restore old authority/epochs; equal ACKs still carry metadata changes and can
reset presentation smoothing. No production admission setter or connect mode
enables this staged path.

### Senior findings carried into the activation gate

| Finding | Disposition |
| --- | --- |
| Enlarging the ACK array must not silently change public queue policy. | Public replacement frames still admit at most eight queued ACKs. The private queue holds 128, flushes at eight, suppresses adjacent duplicates and preserves the existing tail on overflow. |
| ACK metadata is not proof of an authoritative replay baseline. | Accepted command/epoch/Gorilla metadata is staged. Matching recoverable owner snapshots, movement-stat import and replay remain required before prediction is activated. |
| Trusted Gorilla motion depends on the final command duration and sequence. | Wire fields and synthetic sender fixtures do not constitute a live producer. Integrate the real Gorilla author after duration and sequence assignment, before history storage, so redundant commands retain their original authored result. |
| Client pacing changes must preserve the donor server schedule. | The host guard limits private `CL_SendCmd` to once per host/render frame while retaining the donor server catch-up loop. This is a staging measure; local-server scheduling and movement parity remain unproven. |

The diagnostic follow-up found that `svc_strings` has 128 slots but only entries
0–56 are populated. A valid private svc 57 followed by an unknown command passed
a null previous-name pointer to `%s`; existing extension opcodes also lacked
names in shownet/error diagnostics. A single bounded lookup now supplies a
non-null fallback, retains known names and includes the previous numeric opcode
in the error. There is no wire or dispatch change in this follow-up. The focused
production-lookup regression covers svc 57 followed by malformed opcode 127,
other high IDs, null entries and negative/out-of-range inputs; it does not run
the entire server-message dispatcher.

### Verification and remaining reference work

The sender, move-ACK and solid fixtures pass AddressSanitizer and
UndefinedBehaviorSanitizer with real MSG, codec and collision sources and
`--gc-sections`. Their declared diagnostic/network/presentation stand-ins do not
prove gameplay or actual smoothing behavior. Exact commands, source lists and
fixture limitations are in [tests/README.md](../tests/README.md#staged-transport-checkpoint-reproducible-checks).

The requested Ninja directory was recreated with Meson, SDL3 enabled and
`debugoptimized`. The first build exposed a source-only `VectorClear` call in
the real smoothing reset. A WebGPT worker replaced it with the donor's existing
`VectorCopy(vec3_origin, ...)`; the orchestrator independently confirmed the
consolidated Ninja build exits 0. No compatibility macro layer was introduced.
Astra's final bounded source review found no remaining actionable findings in
the ACK parser and guarded diagnostic lookup. It did not execute the tests.

The movement wire reference remains
`1327f795cc2e3a8e4f7c9d68e31d64383930cc00`. Newer source-master changes
`c1b5f2ab` (spatial ambience, weapon identity and VR HUD spacing) and `2857e8b9`
(VRIK pose reset across map transitions) are pending HUD/audio/weapons/VRIK
migration work. They do not redefine this pinned movement-wire comparison.
No live dedicated-peer movement/fire, controller/headset, Windows or ARM
qualification is claimed by this checkpoint.

### Prediction reference correction (user direction, 2026-09-22)

Use QSS-M `03a498aabc411e2e739adc815c5536b161b9626e` directly for the generic
prediction/replay and movement baseline. Earlier sections record how groundwork
was staged; they do not authorize treating the fork's earlier generic prediction
port as the reference for the next implementation. Compare the staged PM solver
with QSS-M, reusing QSS-M code and retaining only justified VR changes.
The fork remains the behavior reference for VR wire data, explicit duration and
authority/epoch rules, roomscale, tracked weapon poses and Gorilla state/authoring.
Source provenance and actual dedicated-peer behavior must distinguish these
layers. The existing admission/owner-state activation gates still apply.
