# Production predictive movement on the vkQuake base

Status: reviewed staged plan; stage 0 demo compatibility is implemented
before production activation or broader mod admission. Baseline evidence:
`3ba35dc8` on `2.0`, 2026-09-27. Local Astra review resolves the first stage;
later activation/ownership decisions still need their bounded disposition.

## Intended outcome

Ordinary desktop and OpenXR players can join the same authoritative server.
Compatible movement uses modern QSS-M-style prediction, sequenced command
history, reconciliation and the inherited VR command additions. Mod-defined
movement remains functional without requiring a different weapon/muzzle
calibration in multiplayer. AD-family mods, including the installed q30a1024
and Mjolnir, are required compatibility cases, not reasons to replace the
server, collision system or QuakeC VM.

Production defaults must make the implemented features usable through ordinary
connection and co-op flows. A private opt-in stock-only trial cannot be the
finished outcome. Conversely, predicting arbitrary QuakeC forces without a
matching client contract would regress mod behavior. Distinguish authoritative
command consumption from permission to replay, and retain a working native
movement owner for genuinely incompatible QC states.

References: QSS-M `03a498aa` for generic predictive movement; inherited product
`1327f795` for VR command/queue/authority semantics; vkQuake base `4bc898f2` for
native desktop physics and engine ownership. Later product changes are mapped
in [source updates](migration-source-updates.md). The user prefers reuse through
adapters and specifically does not require identical vkQuake netcode after the
intentional predictive-netcode improvement.

## Verified starting point

| Claim | Evidence / implication |
| --- | --- |
| The client already offers the pinned private profile on ordinary `pext` negotiation. | Verified in `Quake/cmd.c`, `Cmd_ForwardToServer`; the `connect ... qsvr1` legacy path deliberately bypasses that modern offer. Do not add a second client offer setting. |
| Server private selection and selected PMove are default-off. | Verified in `Quake/sv_main.c`: `sv_qsvr_private` and `sv_private_pmove_walk` both default to `0`. `SV_SendServerinfo` selects the private profile per peer after a matching offer/configuration. |
| Selected authoritative PMove is tied to the pinned stock QC identity and remote multi-slot play. | Verified in `SV_PrivateWalkTrialAdmissionFailure`: stock size/CRC/hash, matching private profile, non-loadgame, remote connection, multi-slot server, WALK/SLIDEBOX and dry initial selection. Custom-stat overlap and customphysics checks are separate conditions. |
| The queue, completion and replay owners already exist. | Verified in `Quake/sv_user.c` private receipt/retirement, `Quake/sv_phys.c:SV_Physics_ClientPrivateWalkTrial`, `Quake/sv_main.c:SVFTE_WriteEntitiesToClient`, and `Quake/cl_main.c:CL_ComputeReplayPlayerMovement`. Reuse them. |
| The selected loop has a world-frame scheduled-Think opportunity. | Verified in `SV_Physics_Client` and `SV_RunSelectedClientThink`; its stack window spans up to eight commands and maintenance/terminal continuations. Pre/PostThink still use command duration. |
| Ordinary server snapshots do not activate public prediction. | Verified in `SVFTE_BuildSnapshotForClient`: owner `pmovetype` starts at zero and is set to WALK only for the selected private owner. Merely negotiating public PREDINFO is not an activation proof. The client has a separate public replay decoder for compatible external peers. |
| Exact q30 has a dormant QC jump adapter, not general admission. | Verified in `SV_PrivateWalkTrialQ30Program`, the selected QC-to-PMove handoff and snapshot permission. q30 retains authored velocity/release flags and snapshots withhold replay. Stock identity admission still excludes it. |
| Stock reconciliation cannot simply be applied to arbitrary mods. | Verified in `SV_PrivateWalkTrialReconcileQCWater` and grounded-jump velocity restoration. These remove stock QC edits; the installed q30 authors map jump heights, boots forces and ladder velocity. See the [exact-program evidence](migration-mod-movement-review.md). |
| Native customphysics now dispatches through the existing QC callback owner. | Verified in `SV_RunCustomPhysics` and native client/entity dispatch. Selected PMove still rejects active customphysics. Do not reimplement the callback dispatcher. |
| A cooperative QC command hook is declared but not integrated. | Verified declaration `SV_RunClientCommand` in `Quake/progs.h`; no invocation or `PF_sv_pmove` implementation is present in this branch. QSS-M supplies both through its existing command and builtin owners. q30 has no such hook. |
| Private demo startup is not currently self-describing. | Verified in `cl_demo.c:CL_Record_Serverdata`: its synthetic serverinfo emits the public extension mask without the selected private-profile marker. `CL_PlayDemo_f` calls `CL_Disconnect`, clearing offered/legacy profile state, and `CL_ParseServerInfo` requires a live offer even during playback. This is a concrete blocker before production private default-on. |

Unverified: connected mixed-client behavior under all mod states; arbitrary
mod/client prediction equivalence; general live native fallback from an already
selected owner; public-server PMove activation; initial/loadgame/local-SP
activation; device/runtime behavior. Earlier tests qualify their documented
slices only. The q30 native/selected 1 ms support difference and larger-tick
position differences are recorded, not silently treated as parity.

## Adapter comparison and reuse

| Option | Reuse / demonstrated incompatibility | Decision proposed for review |
| --- | --- | --- |
| Extend existing selected command owner and its mode metadata | Retains queue, sequenced completion, contact/roomscale/Gorilla state, PMove, native collision, QC VM and packet dialect. Requires explicit QC ownership and safe mode transitions. | Preferred. Keep one command lifecycle and one authoritative completion cursor. |
| Copy QSS-M receipt-time command dispatch into `SV_ReadClientMove` | Reuses its solver/builtins, but would execute QC at receipt as well as vkQuake's world-frame dispatch; its timestamp-derived durations differ from private explicit msec. | Reject the dispatch transplant. Reuse solver/parameter/builtin logic at existing boundaries instead. |
| Replace unknown mods with another continuously living native queued owner | Native collision is reusable, but unaware QC can be cadence-dependent; selected clients currently skip native input acceleration. Terminal continuation does not prove living-command behavior. | Not justified as an automatic fallback. Reopen only with a narrow, explicit command/clock contract and proof that existing native dispatch cannot be adapted. |
| Turn on replay for ordinary native snapshots | Public packet syntax can carry state, but the authoritative solver/callback ownership is not PMove-equivalent merely because the entity is WALK. | Reject blanket replay. Publish replay permission only for a matching solver contract. |
| Port the whole inherited PMove-policy/physics body | Supplies useful policy/reference cases, but brings weaker-model generic movement changes and duplicates the donor's callbacks/collision/lifetimes. | Reuse narrowly verified VR policy and transition semantics; generic movement remains QSS-M based. |

The private protocol already represents authority, prediction permission,
completion and discontinuities. New wire fields require a demonstrated missing
consumer and a review; do not add another feature negotiation or movement
state machine merely to instrument the migration. Exact program identity may
identify a specific verified adapter; it is not itself proof of correct movement.

## Staged implementation

### 0. Preserve private demo recording/playback before activation

Owners: `cl_demo.c:CL_Record_Serverdata`, the protocol header reader within
`cl_parse.c:CL_ParseServerInfo`, and focused demo/header fixtures. Scope: add the
explicit selected profile marker to synthetic private demo serverinfo, and let
offline playback accept the supported marked layout without a live network
offer. Retain all live offer/ordering/extension/base-protocol validation; do not
infer private framing from colliding public flags or replay a demo through
network negotiation. The existing demo writer/reader and disconnect/reset owners
remain authoritative.

Compare early recording's real serverinfo and mid-map synthetic serverinfo.
Exercise marked private and ordinary public playback, duplicate/misordered or
unsupported profiles, incompatible flags and truncated headers. A recorded
private owner update followed by another message must retain framing. Reject
unmarked ambiguous legacy layouts rather than guessing from the shared mask;
new recordings from an admitted legacy connection can emit the explicit marker.
If a small private helper makes the existing header reader independently
callable, move its existing logic within the same owner rather than implement a
second parser for tests. Preserve demo world/reset/VR camera behavior.

### 1. Establish production private negotiation independently of replay

Owners: `cmd.c`, `sv_main.c`, `cl_parse.c`, `host_cmd.c`; existing connection
fixtures. Scope: preserve the current matching per-peer offer and public
fallback, make ordinary 2.0 peers select the supported private transport, and
retain explicit disabling for compatibility/diagnostics. This stage must not
imply that a private connection has selected PMove.

Before changing defaults, verify the actual modern/legacy signon paths,
reference-peer compatibility, private framing and a public peer beside a
private peer. Private transport must carry normal desktop commands and VR
payloads into the same world without manual `connect ... qsvr1` or hidden
server configuration. Public clients must not receive private bytes. A mismatch
must retain an intelligible public/native connection, not falsely advertise
prediction.

### 2. Finish the selected stock movement vertical slice

Owners: `sv_main.c`, `sv_user.c`, `sv_phys.c`, `pmove.c`, `cl_main.c` and existing
movement fixtures. Scope: dry/wet WALK, jumping, command batching/credit,
pause/resume, death/respawn, teleport and pusher boundaries already implemented.
Review unsupported *valid* state handling before making selection automatic:
normal mod/cheat/load transitions must not be treated as malformed packets.
Do not remove genuine finite/framing/identity validation.

Acceptance must execute command production/receipt, authoritative physics,
snapshot parsing and replay together, comparing completion and actual movement.
An ACK-only fixture or an injected `private_pmove_walk_selected` bit is not
production admission evidence. Then change stock selection defaults/labels as
appropriate, retaining the current queue and completion owners. Local-SP
native behavior remains distinct unless a demonstrated requirement calls for
network-style command ownership there.

### 3. Assign AD-family QC ownership at the existing movement boundary

Owners: `sv_phys.c` QC-to-PMove handoff; `sv_main.c` movevar/permission/admission;
`pmove.c` solver inputs; existing mod fixtures. Start with the installed exact
q30 program and keep Mjolnir/other AD variants in the final compatibility matrix.

Complete a contract for jump/release, water drag and ledge jump, boots/ladder/
grapple forces, custom movement/state transitions, teleports, due Think and
maintenance. Keep QC gameplay effects and authored forces; remove only a
demonstrated duplicate engine movement edit. Use native dispatch before
selection for incompatible programs/states. If an already selected state
needs native handling, decide its complete transition before executing any
QC callback; do not restart callbacks after partial consumption.

The existing q30 adapter is a starting component, not the feature's finish.
Do not accumulate dormant per-mod paths indefinitely: each stage must lead to
an ordinary admitted gameplay case or state precisely why its reference
remains native. Compare real QC origin/velocity/flags/forces/effects/timing;
installing a SHA check or testing only a staged solver impulse is insufficient.

### 4. Complete replay contracts and cooperative QC integration

Owners: existing movevar/stat production and client PMove replay; `progs.h`,
QC builtin registry and movement boundary only where needed. First determine
whether a needed mod actually supplies `SV_RunClientCommand` or client movement
code. For a cooperative mod, reuse QSS-M's builtin input/collision logic through
vkQuake's existing world/QC owners; prevent simultaneous native and QC movement.
For unaware QC, expose only the state/parameters needed by a matching replay
contract. A generic client cannot execute undistributed server QC effects.

Enable replay for qualified dry/wet/ability states when the authoritative
snapshot seeds them completely; retain full authoritative movement without
inventing missing forces when replay is unavailable. Reconcile solver/parameter
changes, teleport, death and mode epochs using the current metadata. Cover
public compatible external peers separately from the internal private profile.

### 5. Close mixed desktop/VR and compatibility acceptance

Use one authoritative world with ordinary/public desktop and private VR peers,
then private desktop and VR peers. Include movement, brief jump/fire/impulse,
loss/redundancy, pickups/damage, native and physical attacks, shared calibration,
death/rejoin, map switch and co-op save/load. An ordinary connection must not
require trial commands to reach the implemented behavior.

Collect software evidence after each complete staged slice rather than after
every edit. Existing Linux linkage, offline real-QC/real-hull comparisons,
parser/queue/replay fixtures and connected production paths are complementary;
none alone proves the complete matrix. Defer physical headset/eye testing,
Windows/ARM qualification and performance measurement as requested. Those
deferred checks are listed honestly and do not turn code omissions into
"testing left to the user."

## Open decisions for Astra

1. Is production private negotiation the first implementation stage, or does a
   concrete transport blocker require a narrower correction first? Lean:
   negotiate it normally independently of PMove, preserving public fallback.
2. Can selected stock state transitions be completed by adapting existing
   native continuation, or is a smaller authority/selection change required?
   Lean: reuse current completion/mode owners; do not create a living native
   queued physics implementation without evidence.
3. What is the smallest useful AD-family contract that can actually be activated,
   rather than another dormant q30 branch? Lean: separate native mod compatibility
   from replay permission, preserve QC forces and qualify the full session state
   lifecycle before automatic selection.
4. Where is cooperative QSS QC integration needed and reusable? Lean: integrate
   only for a real consumer, but do not leave an advertised builtin/hook missing.

Rank by user-visible leverage; merge overlapping decisions. Review should
challenge excess gating, duplicated policy and unnecessary replacement. It
must not convert the full mod/VR/predictive outcome into stock-only success.

## Scope and review record

Expected scope is a sequence of small changes at the existing owners, not a
second net driver, protocol, VM, server loop or prediction system. Each stage
needs its own exact write-set contract and a focused implementation update
before coding; reopen this plan when the verified boundaries change. No new
activation/admission code has been changed while drafting this proposal.

Astra reviewed the plan using effective `gpt-6-astra` / `max` settings, verified
from local metadata. Main spot-checked the recorder, disconnect/offer reset,
serverinfo admission, ACK capability checks, producer flag conditions and
client command-history gate before adopting this disposition. The source
review is not runtime certification.

| Recommendation | Disposition |
| --- | --- |
| Repair demos before making private transport the production default. | Adopted as stage 0. Both preconnection raw serverinfo and mid-map synthetic startup need a supported, explicit private decoder. |
| Extract the existing header reader within `cl_parse.c`, with explicit context and a small result type in `client.h`. | Adopted. One MSG reader validates the whole prefix without changing `cl`/`cls` or loading/resetting a world; `CL_ParseServerInfo` commits only a successful result. No new protocol module or persistent state owner. |
| Consume complete recorded optional ACK bodies without live capability prerequisites. | Adopted only for admitted private playback. All flags, body sizes, finite values and state/surface/model bounds remain validated; capability-sent fields are not manufactured. |
| Turn recorded ACKs into live command completions. | Rejected. Playback returns after complete body validation, before ACK expansion, resume handling or reconciliation state mutation. Entity decoding continues. |
| Treat a manually authored trusted body as current server producer coverage. | Rejected. Current `SVFTE_WriteEntitiesToClient` emits the raw Gorilla body, not the trusted flag; the trusted case is inherited-layout codec coverage. |
| Expand the prerequisite into general demo seeking or another movement owner. | Rejected. Keep the existing demo, packet, world-reset and movement owners. Later selection/replay work remains a separate stage. |

Stage 0 source changes follow that exact owner boundary: the mid-map recorder
emits a private marker, the shared prefix reader admits supported marked
playback without a live offer while retaining network admission, and the ACK
reader consumes recorded metadata without enabling causal replay. Both server
activation defaults remain unchanged. A focused Astra follow-up source review
found no commit-blocking production regression. Its P2 request for an explicit
recorded `RESUME_PENDING` case was adopted: the fixture seeds a resume marker
and completable ACK cursor, requires unchanged playback state, then proves that
the identical live body invokes the resume callback and clears the marker.
The follow-up was source review only; the main agent ran the software checks.

Preparation evidence: `tests/negotiation_native_fixture.c` now executes the
actual client offer, server `SV_Pext_f` consumer and `SV_SendServerinfo` writer
with stock `e1m1`/QC loaded. The native Linux build and matrix pass for server
enable/disable, modern/legacy/no-extension/wrong/partial offers, incompatible
base/flags, mixed peer isolation and same-owner serverinfo refresh. Socket sends
are held and the shared production prefix reader now decodes the actual output,
including offline acceptance and live rejection without an offer. Truncated,
unknown, mismatched, duplicated and misplaced private headers leave the result
unchanged on rejection.

The native fixture also executes `CL_Record_Serverdata` through a temporary
file and `CL_GetDemoMessage` for public/private synthetic startup, including
angles and the following signon byte. It executes the actual current server
entity writer through the same file envelope and production entity decoder for
two changed positions in ordinary-private and selected/raw-Gorilla cases. The
selected state is injected to reach the body producer; the fixture verifies the
actual optional flag before decoding and makes no production-admission claim.
No command history, live ACK, prediction permission or Gorilla reconciliation
state is created by playback. This is actual component/file/decoder evidence,
not a complete `record`/`playdemo` world lifecycle or headset image comparison.

The existing ACK fixture passes ASan/UBSan for ordinary, raw and inherited
trusted bodies without live capabilities/history, a following service,
truncation, nonfinite anchors, out-of-range models, and unchanged live capability
rejection. The seeded pending-resume case preserves all client state offline
and triggers the live handler in its control. Existing live ACK, wrap and queue
checks still pass. The full Linux build passes. An isolated negative control
removing only the synthetic private marker fails the native fixture's offline
header assertion (exit 134), demonstrating detection of the original omission.
Stage 1 must
retain this boundary and close ordinary mixed-peer acceptance; these results do
not activate PMove, qualify AD-family replay or complete the migration.
