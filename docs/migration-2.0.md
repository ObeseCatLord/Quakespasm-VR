# 2.0 migration status

## Baseline and preservation

The `2.0` branch starts directly at vkQuake commit
`4bc898f29073e8aa41069f0e79e3cb5a9eb73afa`, the upstream `master` tip checked
on 2026-09-20. Its initial tree was identical to upstream. Subsequent commits
are migration work. `vkquake-upstream` tracks the original repository at
<https://github.com/Novum/vkQuake>; `origin` remains the project fork.
The separate `quakespasm-2.0` worktree avoids modifying the user’s product
worktree (currently on `master`). No history was reset, rewritten, pushed, or deployed.

Behavioral references:

- Inherited base: `8c5a6007a60098b6a5b5c5b552def70e1238a852`.
- Original product snapshot: `7bc466b594e7a7e584dc47879eb6c00f971b01b1`.
- Pinned movement-wire authority: `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`; includes four fixes/package/license commits missing from the OpenXR pin.
- Newer product-master changes `c1b5f2ab` (spatial ambience, weapon identity and VR HUD spacing) and `2857e8b9` (VRIK map-transition reset) are now [inventoried with exact source anchors](migration-source-updates.md) and remain pending HUD/audio/weapons/VRIK migration. The movement-wire pin is unchanged.
- Migration source: `3080841333fa94000df7e1fb9e549c7158685dd6`.
- The original worktree's 13 uncommitted files were copied with a binary patch
  and SHA-256 manifest into the shared Git directory's local-only
  `migration-references/2.0-30808413/` snapshot. The immutable snapshot remains in place; the product checkout is now clean on newer `master`.
  WIP is preserved evidence, not automatically accepted behavior.

The [complete-scope feature map](migration-feature-map.md) is the current migration checklist: 185 behavior/work items with source evidence, destination owners and acceptance criteria, plus [11 optional Ironwail/QSS-M additions](migration-useful-additions.md). The [senior review disposition](migration-feature-map-review.md) records the required early multiview proof and concrete donor differences. Detailed [network](migration-network-map.md) and [renderer](migration-renderer-map.md) maps distinguish donor equivalents from actual ports and goals.

[migration-preservation.csv](migration-preservation.csv) now covers 905 unique paths, extending the original 904-path snapshot with the current master delta. It retains old blob/WIP evidence and adds current-master blobs and feature routing. Exact source anchors and mechanical module routes are labeled separately. [History](migration-history-index.csv) and [public-interface](migration-interface-index.csv) indexes make omissions reviewable. These are scope/audit artifacts, **not proof of completed behavioral integration**. Historical deletions are reviewed rather than restored blindly; preserved WIP is not automatically accepted release behavior.

The sections below record successive checkpoints. The latest implemented slice
is **calibrated private hand-command preparation**; earlier limitations describe
their respective commits, not the current head. None closes the full P1 gameplay gate.

## First source checkpoint

The existing standalone OpenXR backend is adapted to the Vulkan-only donor.
It retains the original session/frame/action/tracker/gaze state machine and
Vulkan attachment API; the legacy OpenGL transport is omitted from this donor
module. This does not remove the requirement to migrate inherited OpenVR
behavior through a Vulkan compositor path.

Meson, the native Unix Makefile and the native Visual Studio project now
include this module as C++, while the upstream renderer remains C. The backend uses SDL2/SDL3 loader APIs and
loads OpenXR dynamically. Vendored headers, licensing and exact source pins
are in `Quake/thirdparty/openxr/PROVENANCE.md`.

**This checkpoint does not enable VR gameplay.** Engine initialization,
renderer/device attachment, menu settings, scene submission and shutdown are
not connected yet. Importing the runtime interfaces does not qualify gaze,
foveation, Vulkan stereo, or any headset. No new CLI switch claims otherwise.
Routine builds/runtime tests are deferred until the implementation slice is
complete, as requested. Source review is not build or device validation.

## Vulkan graphics bootstrap checkpoint

The renderer now honors explicit `-openxr` at Vulkan startup; `-novr` takes
precedence and ordinary desktop startup does not discover OpenXR. Discovery can
fall back to desktop before Vulkan handles are bound. Once the runtime has
selected the binding, creation failures are diagnosed and partial renderer-owned
handles are cleaned rather than mixed with an unrelated device.

`Quake/gl_vidsdl.c` retains donor ownership of the instance, physical device,
logical device, queue and all ordinary renderer resources. It obtains the XR
requirements, uses the existing enable2 creation wrappers and runtime-selected
GPU, and integrates core multiview queries into the donor feature/property chains.
It enables the multiview bit only when supported for two views; geometry and
tessellation multiview bits stay off. This is capability negotiation, not a
multiview render pass. Desktop feature negotiation remains on the donor path.

Runtime requirements and the actual GPU are checked separately from loader
capability. The backend now compares major/minor versions (ignoring patches),
rejects versions below the runtime minimum, and warns above its highest tested
version. The startup selector prefers a known-tested version while honoring
minimums above Vulkan 1.1. Main-thread runtime cleanup also covers errors before device
creation completes, including the Windows fatal path. The failure helper retires
its debug messenger before destroying the instance. No frame-loop device-idle wait or second lifecycle owner was
introduced.

**No session or swapchain is attached by this checkpoint.** Startup logs say so.
Renderer/device bootstrap is the first part of P1; it does not close the early
multiview proof, implement gameplay/input, qualify an HMD, or enable foveation.
It currently retains donor WSI/graphics-queue requirements. Saved VR settings,
menu toggles, OpenVR compositor integration and healthy runtime reattachment
remain part of subsequent integration.

Consolidated software checks after this slice was implemented:

- Native Linux Meson `debugoptimized` build with SDL3 and Vulkan headers passed,
  including the donor shaders and C/C++ executable. This host build is diagnostic
  only; it is not a GLIBC-ceiling-qualified release artifact.
- The [reused Vulkan boundary fixture](../tests/README.md) passed creation-result,
  version/provenance, independent/array-image ownership, incomplete-frame and
  retirement-order checks. It uses simulated dispatch and is not graphics or
  headset proof. Porting it exposed a stale missing event callback in its reset
  setup, which was corrected.
- The build exposed an imported `const void*`/`void*` chain mismatch in runtime
  foveation setup. Linking the mutable-next FB node before the const-next META
  node fixes the C++ type error without changing flags or enabling foveation.
- Whitespace checks passed. Native Windows/ARM, runtime/headset tests, performance
  measurements and the P1 end-to-end gate remain pending.

The code/design review is recorded in
[the bootstrap review disposition](migration-bootstrap-review.md).

## Queue and frame ownership checkpoint

The OpenXR boundary now uses vkQuake's existing queue mutex through a small
callback registration. Attachment requires a complete lock/unlock pair;
registration cannot change while a session exists. The backend locks only the
four runtime calls permitted to access the Vulkan queue, releasing the mutex
before error logging or renderer retirement. Healthy detach preserves this
binding; full shutdown clears it. There is no new submission thread, mutex,
device, or session state machine.

The donor recorder also now consumes the actual WSI acquisition result. If an
image was not acquired, it skips the complete UI/presentation pass and screenshot
readback instead of accessing the previous swapchain index. Scene work remains
on the existing submission path, and pending screenshots wait for a successful
acquisition. Frame-slot bookkeeping now distinguishes retired work from a new
successful submission.

The native Linux SDL3 `debugoptimized` build passes after these changes.
The production frame-recorder fixture covers all OIT/SSAO variants and detects
the original invalid-image access in a temporary negative control. Review and
boundary-check details are in
[the frame ownership disposition](migration-frame-boundary-review.md).
Windows and ARM verification are explicitly deferred until the end.

**Session attachment and stereo scene output remain unimplemented.** This
checkpoint removes synchronization and invalid-image prerequisites; it does not
close the multiview proof or provide headset, gaze, gameplay, or performance
qualification. Image release must still follow actual application submission,
and teardown must retire both recording tasks and GPU users.

## Initial stereo scene integration

Explicit `-openxr` now attempts session attachment at the first renderer frame.
The renderer creates two-layer views of the runtime-owned images and adapts its
existing color, depth, MSAA and transparency resources to the same two-layer
layout. Scene and UI render passes use Vulkan multiview; existing tasks and draws
remain the owners of animation, world preparation, recording and submission.
Main-thread frame completion joins the actual submission task before releasing
both eye layers. Desktop startup remains the default; `-novr` takes precedence.

A dynamic uniform supplies relative per-eye clip transforms to the existing
vertex shaders, preserving donor per-model MVP calculations. Sky uses per-eye
origins. World visibility combines the center and both eye PVS sets, encloses
both full-eye frusta, and retains surfaces facing either eye. Existing SIMD and
indirect GPU culling stay in use; moving/scaled brush backface tests receive a
conservative eye-separation margin. This favors correctness at eye-only visibility
boundaries; its CPU/GPU cost still needs measurement.

Array-aware color/palette/underwater effects use the donor internal color format.
The final XR SRGB target receives the exact inverse transfer before hardware
encoding, preserving the donor gamma/contrast result. SSAO, ray-debug output and
reduced internal render dimensions are not yet array-qualified; their stored
settings are preserved and these paths are ineffective during XR rendering.
No eye tracking or foveation is activated by this slice.

The senior review caught exceptional-frame buffer-retirement and skipped-frame
reference-invalidation defects. Abort cleanup now drains existing donor GPU work
before another begin can rotate dynamic storage without a matching submission;
reference invalidation persists until a valid camera pose is consumed. Window
mode changes retain eye-target dimensions. A desktop screenshot-and-quit smoke
run exposed an additional shutdown race: the render task could still use Vulkan
while SDL retired its driver. Shutdown now joins that task and retires GPU work.

Local verification after implementation:

- Linux SDL3 `debugoptimized` build, including desktop and stereo shaders, passed.
- Production-boundary ownership and render-pass/recorder fixtures passed across
  desktop/stereo, all transparency modes, MSAA off/4x and SSAO requested on/off.
- Independent projection checks and production camera restoration/reference/
  abort-boundary checks passed. Wait/creation spies do not establish GPU validity.
- On this machine's RTX 4090, the stock `start` map rendered with task rendering
  and 4x MSAA; its screenshot was visually inspected. After the shutdown fix,
  the same screenshot-and-quit sequence completed with exit status 0. The test
  used an isolated temporary config and read-only links to existing game packs.
- Windows and ARM remain deferred until the end. No performance gain, multiview
  GPU validation, actual headset presentation or gameplay parity is established.

**Experimental renderer integration, not completed VR gameplay.** Current head
translation uses a temporary initial-pose anchor and the inherited default world
scale. Tracked locomotion, controller/weapon behavior, options toggles, physical
VR HUD/menu placement, the desktop mirror and OpenVR compositor support remain
migration work. Eye-only visibility, moving/scaled models, task-enabled multiview,
array effects, pause/recenter/restart and real pending-GPU abort recovery still
need an actual multiview scene test; P1 remains open. See the
[stereo senior-review disposition](migration-stereo-review.md).

## Initial checkpoint review

Astra (`gpt-6-astra`, xhigh), 2026-09-20, reviewed the introduced backend and
build changes against `30808413`. No concrete introduced defects were found
within that source-review scope. Main review confirmed that the dynamic loader
is reached only through explicit OpenXR preparation and that renderer retirement
still precedes runtime image destruction. The interrupted earlier audit was not
counted as a completed review.

| Review item | Disposition |
|---|---|
| SDL2/SDL3 loader types and base-path ownership | Retained after source review |
| Vulkan session/image lifecycle preservation | Retained; donor submission/mutex integration still required |
| No installed runtime needed for desktop startup | Preserved by dormant, explicitly invoked loader discovery; runtime qualification pending |
| Meson, Unix Makefile and native Visual Studio declarations | Source-reviewed; builds deferred |

Visual Studio project XML is well formed and retains its original BOM/CRLF.
The ten unchanged dependency/license files match the source byte-for-byte.
Whitespace checks pass for adapted project files; pre-existing whitespace in
vendored `openxr.h` is intentionally preserved. The original thirteen-file WIP snapshot was verified at that initial checkpoint;
newer master now supplies the product checkout. None of these checks establishes
build success, working VR, desktop runtime parity, or a performance improvement.

## Local multiview GPU qualification

A real local Linux run now renders stock `start` through Monado's simulated
OpenXR device and the host RTX 4090. Khronos synchronization validation exposed
two concrete defects: delayed SDL window-size events corrupted the eye viewport,
and animated ray-shadow geometry reused AS scratch storage with incomplete
memory dependencies. Both are corrected at the existing donor owners. Astra
also found an off-by-one `maxVertex` bound, now corrected in both size and build
descriptions. See the [senior review disposition](migration-local-gpu-review.md).

The consolidated debug-symbol SDL3 build and production resize fixture pass.
The live check ran 1,136 scene frames with task rendering, verified all twelve
effective OIT/MSAA/indirect combinations, exercised palette processing, paused,
restarted the desktop window at 800x600 while preserving 896x1007 eye targets,
then quit normally. It produced no Vulkan validation errors or synchronization
hazards. Initial and post-resize compositor images were inspected and show full
scene output with different eye views. The environment used Monado
`v25.1.0-710-g735e29e4e`, NVIDIA 610.43.03 and validation layers 1.4.350.1.

Earlier startup-script loading-screen captures are explicitly excluded from
scene evidence. The reproducible [local GPU smoke gate](../tests/README.md#local-openxr-gpu-smoke)
waits for signon and checks effective renderer state. The runtime, config and
game data directory were isolated; headset/eye testing was not needed for this
checkpoint. This is renderer qualification on one machine, not full P1 gameplay
acceptance or a performance comparison. Windows and ARM checks remain deferred.

## Inherited floor, scale and comfort view adapter

The inherited `vr_world_scale`, `vr_floor_offset` and `vr_viewkick` settings now
work through existing donor view/render owners with their original defaults and
flags. Head height and eye separation use the same scale as the stereo clip
transform. A floor-known runtime uses player-relative physical height plus floor
offset, replacing desktop eyeheight; `LOCAL` keeps relative-height fallback.
Physical head displacement uses yaw only so desktop pitch cannot tilt the floor.
The saved view base retains its own viewheight for correct paused behavior.

Inherited bob, movement/death-roll, drift, bounds and kick gates are adapted;
idle motion and stair smoothing remain. Invalid/nonpositive or overflowing scale
uses an effective default without overwriting the saved setting. The runtime
publishes floor metadata from its existing space owner. Eye tracking remains
optional and inactive, with no foveation fallback introduced.

The Linux build, production view/camera and backend fixtures passed. A live
Monado check passed 19 probes over 1,586 scene frames, including scale/floor
changes while paused, invalid-scale recovery and chase-camera transitions, with inspected eye images,
normal exit and no Vulkan validation errors. See the
[source comparison and review disposition](migration-view-adapter-review.md).
This is partial VR-003/VR-004/VR-014 progress, not P1 completion: camera/command
aiming, recenter/server-yaw, tracked weapons and roomscale authority still need
their coherent inherited implementation. No performance improvement is claimed.

## Inherited head/mouse aim and camera integration

`vr_aimmode` (default 7) and `vr_deadzone` (default 30) now reuse the inherited
resolver through the donor view/input owner. Modes 1–6 update actual command aim;
the visual view remains separate and enters stereo preparation exactly once.
Mode 7 retains its inherited default and head-driven visual view, but tracked
controller command aim still awaits the coupled hand/weapon/movement migration.

Absolute server angles, relative angle deltas, setview/readback, centerview,
client reset and tracking-origin rebases now have distinct handling. Server
angle locks preserve authoritative commands while visual head motion accumulates;
unlock publishes that accumulated aim once. A hidden weapon cancels an accepted
gameplay yaw target at message completion even if no frame is rendered. Standalone
setview alignment retains separate request provenance. Same-world runtime
recreation keeps yaw history; a new client clears it.

The donor chase calculation now receives resolved visual angles and retains its
collision result. Its saved contribution also allows paused head rotation to
remain current. Existing input devices, command construction, packet formats,
render tasks and camera collision remain their original owners.

Source comparison, Astra review dispositions and local evidence are recorded in
[migration-aim-review.md](migration-aim-review.md). This advances VR-005 and
VR-014; full controller/roomscale, scripted-camera/demo, tracked weapon and
network parity remain open. P1 is not complete and no speedup is claimed.

## Coupled movement groundwork

The inherited PMove solver and Gorilla/roomscale algorithms are now staged with
their command types and required boundary declarations. The narrow adaptations
use donor hull collision, the active VM's world model and a session-active view
query. The three Gorilla headers remain byte-for-byte source copies. The solver
is not yet linked into the gameplay executable; no private protocol capability
or new prediction path is advertised.

The Linux executable builds with the extended shared types. A focused solver
fixture passes walking, jumping, freeze, roomscale once across substeps, outlier
rejection, boxes, rotated brushes, stationary solid and water cases through both
actual donor hull implementations. This is groundwork evidence, not gameplay
parity or a performance measurement.

The [Astra-reviewed movement plan](migration-movement-review.md) sets the next
activation milestone: explicit private-dialect admission plus command timing,
redundancy, authoritative baseline and prediction replay, followed by tracked
movement/fire against the pinned dedicated server. Local single-player bypasses
the source prediction policy and cannot prove that path. The new server side
then reuses that command contract while retaining ordinary donor peers.

## Private command codecs and collision bounds

The pinned fork's command-body serializer and parser now live beside the donor's
existing input owners. They preserve explicit milliseconds, cursor input, tracked
hand and roomscale data, akimbo, weapon contacts and raw/trusted Gorilla payloads.
These are staged codecs: no production caller selects the private connection
layout yet. Public extension masks do not authorize the incompatible dialect.

Entity deltas now retain received collision bounds, which the donor previously
read and discarded. Public packed bounds keep their wire format; inherited
variable-size tags require the explicit private layout. A focused test connects
the actual snapshot decoder to PM weapon queries and donor collision tracing.
This corrects client collision data for peers that send it; the current donor
server does not emit those bounds itself.

The shared float reader now detects truncated packets before accessing bytes,
and packed solid decoding uses an unsigned shift while preserving the zero
non-solid sentinel. Replacement removals clear stale collision state, and the
existing network collision owner handles negative upper bounds and legacy
tracer metadata correctly. Astra's source comparison
found no command-layout mismatch and recommended rejecting nonfinite private
command scalars. See the [implementation review](migration-movement-review.md)
and [local verification instructions](../tests/README.md) for scope and results.
The final Linux executable builds; both codec/collision fixtures pass with
AddressSanitizer and UndefinedBehaviorSanitizer.

At this codec checkpoint, private admission, command pacing/history,
completed-simulation acknowledgments, authoritative state and replay remained
the next coupled integration work. The staged transport checkpoint below adds
partial implementations. This
checkpoint does not establish networked movement, controller or headset parity.

## Staged private transport and ACKs

The existing input/parser owners now stage pinned command duration and redundant
history, ordered transport ACK drain, and atomic movement-ACK metadata/Gorilla
state parsing. Public replacement ACK admission remains eight entries; private
capacity is 128 with an eight-entry flush threshold. Explicit pinned selection
is required; neither FTE bits nor svc 57 alone admits the private layout. No
production admission setter or new connection mode is enabled.

The host guard preserves donor server catch-up while sampling private client
commands once per host/render frame. Local-server parity remains unproven. Real
Gorilla authoring must run after duration and sequence assignment and before
history storage; synthetic wire payloads do not qualify that producer. Received
ACK metadata likewise does not prove a matching recoverable owner snapshot or
prediction replay. Those remain coupled activation requirements.

On 2026-09-22 the sender, move-ACK and solid production-source fixtures passed
ASan/UBSan. A narrow diagnostic fix prevents null command names above svc 56
from reaching shownet/error `%s` formatting, with bounded fallback for invalid
indices and a focused production-lookup regression. This is not a full service
dispatcher or live-peer test. The [movement review](migration-movement-review.md#staged-transport-and-ack-checkpoint-2026-09-22)
records the senior findings and the [test instructions](../tests/README.md#staged-transport-checkpoint-reproducible-checks)
contain exact reproducible build/sanitizer commands and fixture limitations.

The consolidated Linux build passes after replacing the source-only
`VectorClear` with the donor's `VectorCopy(vec3_origin, ...)` in the real
smoothing reset. The orchestrator confirmed Ninja exit 0. The WebGPT worker
applied the code fix but disconnected before returning its report; build status
was verified independently. The sender/ACK/collision sanitizer results above
remain focused checks, not a live gameplay qualification.

For the next implementation, QSS-M commit
`03a498aabc411e2e739adc815c5536b161b9626e` is the primary reference for generic
predictive movement, the movement solver and replay. The user identified the
fork's generic prediction as an earlier weaker-model port; it is not the
implementation authority. The fork remains the reference for VR networking,
explicit command durations/authority epochs, roomscale, tracked weapons and
Gorilla behavior. Audit the staged solver against QSS-M and preserve justified
VR additions instead of copying the fork's generic prediction wholesale.

## QSS-M movement baseline correction

The staged solver has been compared directly with QSS-M `03a498aa`, retaining
the donor collision adapter and justified VR specializations. VR ladder movement
still ignores head pitch for climbing. Elevated mod jump speed no longer serves
as a proxy for a VR command. Legacy touch processing follows QSS-M's adjacent
repeat rule, while private explicit-duration commands retain one impact per
entity across substeps. The valid-position recovery fix remains intentional.

The [provenance note](migration-qssm-pmove.md) inventories retained differences;
the [local Astra/max review](migration-movement-review.md#qss-m-solver-audit--local-astramax-2026-09-22)
records their disposition. The solver is still not linked into the game and
prediction is not enabled. Local VR using head/mouse aim needs an explicit
swimming policy before activation: the pinned local server recognizes those VR
players even when their commands lack the controller-specific VR wire flag.

The focused solver fixture passes ASan/UBSan with both donor hull implementations,
including ordinary and explicit-duration movement, VR ladder/swim distinctions,
touch ordering and valid-position recovery. This is source-level movement
validation, not live dedicated-peer, QuakeC callback, headset or performance
qualification. No Windows or ARM check was run in this slice.

## Production client movement linkage

The shared PM solver and client snapshot/weapon-query adapters now compile and
link in the Linux engine. Meson, common Make and Visual Studio source lists
include the module. The previously staged server wrappers had no production
callers and depended on missing authority/co-op functions; they were removed
following local Astra/max review. Their code remains preserved at `6aafc918`
for integration with the real donor server owners.

QSS-M serverinfo fallback now supplies client movement settings independently of
local server cvars or tracking state. Existing serverinfo callbacks refresh that
cache, client-state destruction invalidates it, and current protocol flags are
selected at use time. Public QSS-M and explicitly admitted private VR stat
formats remain distinct. The parameter selector rejects invalid used numeric
stats; it does not certify authoritative replay state or atomic configuration
delivery. The [local Astra stat review](migration-movement-review.md#incremental-movement-settings--corrected-activation-contract)
confirmed that explicit receipt of every stat is an invalid activation gate:
unchanged zero defaults may never be transmitted. Replay will retain QSS-M
incremental-stat semantics and select settings once per pass.
The review also caught command-argument truncation that could change movement
values. Argument storage now matches the existing tokenizer, incomplete
serverinfo commands do not change settings, and the network command owner
rejects truncated text before dispatch.

The full Linux SDL3/debugoptimized build and focused movement-parameter,
movement/collision and private-solid sanitizer checks pass. An executable-level
wire-command check also passes for exact-fit, oversized and unterminated strings.
Windows/ARM source
lists are updated but have not been verified. The [solver provenance note](migration-qssm-pmove.md#production-client-linkage)
records fallback differences and fixture limits; the [review disposition](migration-movement-review.md#client-solver-linkage--local-astramax)
records why this dependency correction was needed.

At that linkage checkpoint, client replay, matching owner snapshots/ACKs,
private admission and the real server movement owner were pending. Linkage alone
did not enable prediction; the following checkpoints record subsequent work.

## Private owner/ACK association

The receive path now pairs accepted private movement acknowledgments with the
matching self-contained player update, publishing that association only after
the complete server message has been parsed. Later ACKs, view changes, removals,
missing owners and malformed trailing data cannot reuse an earlier candidate.
Synthetic local ACKs also invalidate a previous association. The existing entity
state remains authoritative; the added fields are metadata, not another snapshot.

The Linux build, focused sanitizer checks and actual whole-message parser probe
pass. [Local Astra/max review](migration-movement-review.md#private-ownerack-association--local-astramax)
found the local ACK invalidation gap, which is fixed and covered by the sender
fixture. Association is separate from permission to predict; admission and
replay were still pending at that association checkpoint.

## First live pinned-peer desktop connection

The current integration work adds explicit `connect <server> qsvr1` admission
for unchanged `1327f795`, while ordinary `connect` retains public behavior and
server discovery. The connection-owned selector survives changelevel but resets
on disconnect, failed connection, local startup and public demo playback. The
header must match the entire pinned RMQ/extension/coordinate tuple; public bits
alone never select private framing. The live signon test exposed a further
private `svc_time` difference, now isolated at the existing reader.

Direct QSS-M command replay uses the existing journal, collision collector and
shared solver. Private prediction additionally requires matching owner/ACK
association, permission and movement authority. The QSS archived `cl_nopred`
control is restored. Settings remain incremental and are selected once per pass;
invalid settings suppress replay without blocking network recovery.

Basic local desktop gameplay against the unchanged pinned dedicated peer now
passes: signon, authoritative movement, firing (25 to 21 shells), position
settling, and `e1m1` to `e1m2` changelevel/re-signon with renewed prediction
permission. Separate lifecycle checks pass public rejection of the private
header, error teardown, failed private connection cleanup, private reconnect,
local-map startup and public demo playback. They use isolated profiles with
canonical Straight archives and debugger-injected held keyboard state.

The Linux build, ten full-capacity server-info checks, seven time/version
checks and nine initialized header cases pass. Server-info capacity is committed
in `de89a115`. The [replay senior review](migration-movement-review.md#replay-presentation--second-local-astramax)
found partial-command categorization/smoothness and attachment/trail problems.
The follow-up now uses non-consuming input previews, a disposable partial solver
step and one shared viewentity pose per relink. Input immutability, actual PM
underwater categorization and attachment ordering checks pass. The rebuilt
client also passed a live higher-render/lower-send-rate check, with 21 observed
position updates between unchanged command/authoritative states. Final local
Astra review found a public send-frame timing issue: the preview now uses the
pending duration cleared by sending, avoiding positive-time simulation of cleared
device axes. Its focused production-owner host-order regression passes under ASan/UBSan,
covering held/released joystick input, alternating sends and repeated previews. The server authority port, tracked command producers,
physical VR behavior, delayed/lost delivery, stair/swim qualification and
private-demo parity remain open. This is not completion of the coupled VR proof.

## OpenXR controller button input

Completed OpenXR action samples now feed the donor's native key-binding and
command path before `Cbuf_Execute`. The adapter composes the existing OpenXR
profile mapping with inherited controller behavior, including handedness,
trigger hysteresis, menu navigation and Index pad/stick distinctions. It adds
the three VR bind names without changing existing key numbers or user bindings.
Focus, role/profile, destination, capture and native-clear transitions release
owned keys and require neutral input before rearming.

Fresh right-trigger presses activate selected menu items through Enter; binding
capture receives the actual trigger key. Blocking confirmation dialogs refresh
through the existing serial XR frame owner and accept controller confirmation
without advancing the host simulation. Failed or skipped sampling invalidates
input. Native held-key rebinding now releases the old action before replacing
its binding. The [input review and evidence](migration-input-review.md) records
source mapping, local Astra design dispositions and verification limits.

The Linux build, adapter sanitizer checks, native key-name checks, action-driven
stock-map movement/firing/focus proof and isolated simulated-Monado modal checks
pass. The 55-checkpoint native lifecycle probe and expanded modal checks cover
real alternate bindings, held-key rebinding, menu capture, submenu boundaries,
unrelated simultaneous controls and ALT-active confirmation. Local Astra review
findings were addressed in the existing owners. These checks do not qualify
physical controllers or a headset; the linked review records their limits.

Analog locomotion and turning, hand/muzzle and roomscale command production,
weapon presentation/wheel, menu pointers, haptics and the complete inherited
binding/default migration remain open. This increment does not close P1 or the
full VR input feature rows. Windows/ARM64 and device/eye-tracking qualification
remain deferred; no performance gain is claimed.

## OpenXR analog locomotion and local turning

Head-relative, offhand-relative and raw stick movement now use the inherited
calculations through the existing OpenXR input adapter. Snap, smooth and queued
180-degree turns use the tracked-view yaw owner. The controller gun-angle
transform is reused for movement/RAW command orientation; this does not provide
complete muzzle, weapon or roomscale data. Eye-tracking/foveation policy is
independent of this increment.

The native pending command retains the derived VR movement and angle basis.
Send and prediction preview share nonconsuming assembly; server corrections can
discard VR motion without deleting desktop input. Public packets now serialize
prepared command angles, matching private command handling (`36bfa39e`). Native
keyboard run scaling is applied once, alongside the inherited vanilla-run
compatibility control. Pose, focus, authority and controller-profile changes
apply neutral gates only to the channels that depend on them.

The Linux build, affected sanitizer checks, actual codecs and seven-mode camera
checks pass. The initialized simulated-Monado smoke passes actual server movement
in all three modes and both handedness choices, adjusted-camera turning,
focus/rearm, mixed inputs, repeated previews and real native no-send/catch-up
schedules. The runtime supplies an HMD; controller poses/actions are injected at
the completed-frame boundary. These results do not qualify physical devices or
performance. The [locomotion review](migration-locomotion-review.md) records
source reuse, adaptations, senior-review findings and exact proof limits.

Hand/muzzle and roomscale producers, weapon presentation/wheel, menu pointers,
haptics, full binding/default migration and VR-specific swimming/ladder behavior
remain open. This increment does not close the full P1 gameplay gate.

The [tracked command and weapon-use gate](migration-vr-command-producer.md)
compares the narrow adapter with a subsystem transplant and records the missing
server weapon consumer. A command-lifetime roomscale adapter is staged and
locally build/fixture checked, but cannot yet activate private VR movement.
The first vertical proof must follow a calibrated muzzle and roomscale command
through actual movement and QuakeC weapon use.

The current `2.0` checkpoint also has the inherited body-relative raw hand
transform, a shared floor/LOCAL eye-height reference, the pure calibrated
aim-offset transform, and the donor's model-space left-hand muzzle reflection.
An Astra review caught a focus-loss camera jump; the view owner now retains
the body-relative eye anchor through temporary input loss and mode changes,
while the local server remains excluded until its private receiver is wired.
The Linux build and focused stereo-camera/locomotion fixtures pass. The active
viewmodel's schema profile and relative muzzle command producer are staged and
Linux build/fixture checked. The tracked visible weapon adapter and unchanged
pinned-server gameplay proof remain open, so these checks do not establish live
VR firing parity.

A first private OpenXR firing slice now passes with a simulated HMD and
synthetic controller against the unchanged pinned dedicated peer. It checked
focused stereo/private signon, the active stock shotgun calibration, a finite
relative VR attack command, a covering server ACK and authoritative shell
consumption. This narrows the remaining proof: shot origin/direction, damage,
body/eye/hand alignment, blocked roomscale movement, visible weapon placement
and physical device behavior are still unqualified.

## Next integration gates

Follow the [reviewed architecture plan](vkquake-base-migration-plan.md).
The next bounded end-to-end slice is:

1. The code-level runtime/instance/device bootstrap is now implemented and
   Linux-build checked. Qualify its runtime GPU/WSI combinations as session
   integration proceeds; retain the existing owners.
2. The initial OpenXR attachment and task-enabled scene are implemented and
   locally GPU-checked. Qualify moving/scaled models, eye-only visibility,
   reference changes, abort and runtime-loss recovery before closing P1. Retain
   the existing submission join and task/GPU retirement boundaries.
3. Reuse existing VR view/input/gameplay algorithms for an actual map, weapon,
   movement and HUD. Update gameplay/particles once per logical frame. Preserve
   independent live avatar poses and their shadow poses within donor models.
4. Establish task-enabled opaque multiview with moving brush lighting, restart,
   focus loss and shutdown before expanding the bulk migration. Then qualify
   Linux (including ARM64) and native Windows; headset testing remains a
   separate user checkpoint.

The donor integration points are `GL_InitInstance` / `GL_InitDevice` and the
existing submission task in `Quake/gl_vidsdl.c`. Keep its queue mutex: OpenXR
begin/end-frame and swapchain acquire/release calls can also access the bound
queue and must be synchronized with donor submissions. Released color images
must have the required attachment layout and all application accesses submitted;
GPU completion before every release is not required. Use completion waits for
resource retirement, rather than a new per-frame device-idle stall. These rules
come from Khronos's official
[`XR_KHR_vulkan_enable2` specification](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable2.adoc).

Save dialect validation must precede world/game changes: the fork's multiplayer
version 6 conflicts with donor KEX version 6. Prediction, command timing and
server changes are one coupled migration, with explicit peer revision fixtures.
QuakeC callback ownership must remain coherent when donor GUI work uses tasks.
These are prerequisites, not optional cleanups after porting files.

## Runtime and performance requirements

- Windows, Linux, and Linux ARM64 (Steam Frame standalone) each require both
  OpenXR VR and desktop support. Linux/Monado/Beyond 2e remains the primary
  headset configuration. Steam Frame PC streaming is also a release target.
  No eye provider is assumed; desktop operation must not require an installed
  OpenXR runtime or connected headset.
- Eye tracking is an optional VR-menu toggle, off by default. Missing/invalid
  gaze renders full quality. Fixed foveation is explicit opt-in only, never a
  default or automatic fallback. Runtime support must be detected; the presence
  of a headset name or an extension declaration is not proof of working gaze.
- Reuse donor multithreaded loading/rendering, resource ownership, GPU batching,
  precision and asset-format support. Add conservative two-eye visibility and
  single-pass stereo; foveation must not cull visible peripheral geometry.
- Preserve all inherited/project behavior, including OpenVR, weapon wheel,
  prediction/co-op, saves, QC/mod compatibility, avatars/FBT, physical melee,
  Gorilla movement, akimbo, audio/voice, catalogue and configuration/assets.
- Compare complete frame CPU/GPU time and loading on large maps including
  Mjolnir `mj4m1`. No speedup is claimed before comparable measurements.

## Upstream maintenance

Keep `vkquake-upstream/master` as the untouched donor tracking ref. Merge reviewed
upstream updates into `2.0` using ordinary ancestry; do not replay the old fork's
unrelated history or repeatedly cherry-pick the entire renderer. Keep product
policy at existing narrow boundaries and record donor conflicts by subsystem.
Avoid mass formatting, renaming donor files or replacing its working owners.
Retain source licenses and provenance for each transplanted subsystem. Review
conflicts against both the product behavior reference and the donor's change.
Branch ancestry makes merging possible; it does not promise conflict-free merges.
