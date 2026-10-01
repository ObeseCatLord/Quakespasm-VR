# End-of-implementation Linux and ARM qualification

2026-09-30. Planning only: do not run this pass until surviving implementation
requirements are closed. Earlier source checkpoints and historical test results
do not qualify the current tree. Current user scope decisions override older
acceptance text. This plan does not shrink the185-feature inventory to recently
changed code.

## Entry and evidence ownership

First reconcile every migration-feature-map.csv row against its current
production owner and linked plan. Distinguish source-integrated/native-reused,
concrete implementation gap, software qualification pending and user-excluded.
A matching name, symbol, number, archived comment or absence of TODO is
insufficient. Review the512 MAIN/XR QC interface entries and inherited command/
setting index at their actual dispatch/consumer boundary; intentional native
improvements and retired settings need explicit treatment, not blind aliases.
Do not mark a row complete because a narrow neighboring fixture passes.

Use the existing Meson and native packaging recipes. Build the complete desktop/
OpenXR client and required dedicated components on this Linux machine, then
from a pinned source snapshot in an isolated directory through ssh foundry
on native Linux ARM. Qualify the same voice/codec/Steam Audio feature selection
and generated shaders on both architectures. Inspect linked dependencies and
installation artifacts. A no-SDK fallback build is additional coverage, not a
substitute for the required native audio package. Do not modify a running game/
server or deploy to Foundry. Windows remains a release target with builds deferred.

Reuse existing tests/README.md recipes and native runners where they cover the
current contract. There are119 files under tests at this checkpoint; neither
that count nor an aggregate green result proves scope coverage. Select applicable
cases after reading the actual fixture boundaries and current code. Add only
missing meaningful end-to-end software coverage after implementation. A fake
runtime, packet counter or helper test must keep its proof limitations explicit.

## Full surviving scope matrix

The [native entity-search adapter](qc-entity-search-2.0-plan.md) needs actual
server QC find/nextent with all four program fingerprint functions and with one
missing: disconnected non-free player holes, later active players, ordinary
entities, exhaustion, program changes and real co-op centerprint/teleport use.
Check native CSQC/disconnected-body enumeration and all five extension search
handlers remain unchanged. The inherited [round-query adapter](qc-inherited-round-search-2.0-plan.md)
also needs actual win/loss query ordering, no-result latch assignment, rounds
reset, opposite monster clearer and precise cleanup-enemy predicates across
supported targetname fields. Check other maps/non-target fields/CSQC, same-map
restart, spawning loads and fastloads with the retained reference static latch.
These source patches are not a whole-VM or malformed-field proof.

The [wheel tracking-anchor adapter](migration-wheel-tracking-anchor-2.0-plan.md)
needs actual shared draw/hit/release behavior through opening, body translation,
snap/smooth turns, camera pitch/roll/kick residuals, physical hand/head movement,
scale/floor/reference changes, both handedness and ring changes. Captured grip
calibration stays fixed while the live wheel pointer uses current calibration;
view mode follows the current full headset view. Verify world model/action
occlusion, foreground native MSAA resolution and desktop/no-runtime selection.
Live headset comfort and performance remain the user's later validation.

The [generic liquid adapter](predictive-generic-liquid-2.0-plan.md) requires
actual nonstock loaded QC through the existing shared command owner: ordinary
swimming with QC-authored velocity followed by shared PMove, positive pending
forecast and authoritative correction, and dry/wet roomscale crossings without
a new QC lifecycle or lost suffix. Cover accepted batching versus world QC
clock, swim press/hold/release, damage/sounds/timers, authored waterjump and
solver-created ledge handoff. Native handoff must receive the completed solver's
horizontal movedir before callbacks; callback cancellation/changed velocity/
movedir/deadline must survive, private timers must retire, and the current head
must close exactly once while the next unstarted head remains queued. Stock,
q30, hold/ladder/customphysics/cooperative, pause/load/relocation and native
return keep their established contracts. Arbitrary server QC corrections are
explicit; exact generic swimming forecast is not inferred from shared PMove.

The [current foveation checkpoint](openxr-foveation-source-checkpoint-2.0.md)
requires selection and actual rendering across KHR layered/shared rate maps,
FB/META paired maps and ordinary stereo: changed versus unchanged KHR uploads,
static versus eye profile setters, validity/stability/opt-out/focus transitions,
two-eye offset granularity, density restoration failure and ordinary-pass
recovery, protected materials/full-rate fine depth and native MSAA/AO/OIT.
Explicit fixed remains usable independently of the eye toggle; opting out of
eye tracking in eye mode must select full rate, never fixed. Preserve native
desktop selection and complete either-eye visibility. Borrowed-image metadata
and incoming-readiness assumptions remain those of the accepted interoperability
decision; software path coverage must not be presented as live runtime proof.

The [reusable AD preset](ad-weapon-preset-2.0-plan.md) requires actual native
menu/console callback selection0–5 and reload in a generic/unknown AD context,
known AD/additional-root contexts and existing fixed/enhanced contexts. Cover
live15-row targeting versus reload Copper/conditional LimJam fallbacks,
derived versus authored muzzles, independent enhanced/source/melee fields,
invalid/range values, capacity refusal, saved selector and authored reload
precedence. The concise AD label must remain inside the native menu column.
This is explicit contextual selection, not automatic mesh/family recognition.

Native diagnostic fields/bboxes and optional controls also need actual
collector/VM/GUI consumers, local/multiplayer gating, line/string bounds and
desktop/stereo rendered output. MOD-014's explicit tracked panel is now
source-integrated in a42de70f; source presence alone does not establish readable
stereo output or validate the canvas/viewport inversion at actual resolutions.

The [inherited controller-axis adapters](controller-axis-parity-2.0-plan.md)
require backend-sample-to-Key_Event-to-Cbuf/native command coverage: gameplay
weapon-hand horizontal bindings; Vive positive/negative/center rising clicks,
held sector changes, repeated samples and logical hand swaps. Exercise profile/
focus/role/context neutral rearm and callback dispatch aborts. Suppressed clicks
must still become owned so closing an already-active wheel/calibration or moving
from center to a sector while held cannot inject a delayed cycle. Include
calibration ending/canceling inside Commands and native same-sample queued
wheel-open ordering; do not claim execution-time exclusion. Other profiles,
menu offhand navigation, vertical wheel input and paused private impulses retain
their existing owners. Physical controller qualification stays user-side.

The [music format-transition repair](music-format-transition-2.0-plan.md)
requires actual native track replacement across rates, mono/stereo and8/16-bit
input, including replacement after EOF with no live decoder, repeated stop,
same-format replacement, bounded queue progress, natural EOF tail drain,
pause/loop and audio restart. Qualify SDL2/SDL3 configurations where supported
and native fallback; preserve the documented bounded already-mixed remainder.

| Family | Required software evidence at the actual owner |
| --- | --- |
| Native base and delivery | Desktop map/menu/console/config and built-in vkQuake desktop demo behavior; build/install/packaging and resource lookup; requested shared changes without replacing native vkQuake ownership; documented upstream adapter provenance and a separate disposable upstream merge rehearsal. No VR demos or additional demo feature gate. |
| OpenXR session and input | Device/runtime qualification, session/frame acquire/wait/release/submission/retirement ordering; startup, compatible attachment, healthy disable and explicit recovery; optional trackers/gaze, focus loss, physical-hand/profile changes and held-key release. Exercise actual backend/renderer paths where software facilities exist; do not call a fake-runtime result headset proof. |
| Stereo and native graphics | Actual two-layer opaque multiview shaders/passes and independent eye cameras; once-per-frame animation/gameplay/particles/QC; native Vulkan MSAA/precision/lighting/warps/transparency, protected UI/weapon detail, shared AO qualities with unchanged desktop AO and stereo VR AO; conservative either-eye PVS/frustum/backface/moved-brush rejection independent of gaze. Rendered software evidence must accompany eligible path coverage. |
| Foveation | FB/META preference and device-time fallback eligibility, exact extension/feature-family ownership, density-map/image lifecycle and MSAA compatibility; gaze toggle, freshness/validity and unavailable eye tracking restore full quality; fixed mode is explicit only, never default/fallback. KHR is a startup/device fallback, never switched live onto an FDM-created device. No quad views. |
| VR movement and presentation | World/floor scale, recenter/roomscale, head/hand aiming, snap/smooth/180, authoritatively aligned camera continuity, crosshair depth/muzzle transforms, inherited HUD/console/scoreboard/menu placement, pointers/sliders/keyboards and mirror/hidden-area behavior. Gesture-only melee suppresses the recognized melee trigger and presentation attack animation while preserving QC damage/timing; ordinary desktop/ranged attack remains. |
| Wheel, calibration and avatars | Stable schema IDs, eligibility/remembered selection and real-action clearing/release, desktop/VR sessions and cancellation, co-op action control, copied profile/preset/global save behavior and one solo/MP muzzle/weapon transform; native generated paired models and haptics, player/avatar/equipment/VRM/FBT endpoint identity and independent per-instance poses/shadows/bounds. |
| Prediction and multiplayer | Real native loopback transport, signon and server-produced snapshots/parser, public desktop/private VR negotiation, command duration/redundancy/ACK/replay/cadence, packet loss/reorder/reconnect/teleport/map/save discontinuities, custom QC movement and CSQC entity/events; mixed desktop/VR play and ordinary source-authorized commands. Header-only synthetic cases cannot replace actual owner round trips. |
| Co-op and saves | Classic opt-out and shared pickup/equipment/keys/ammo policy, callbacks/targets, collision/friendly-fire/telefrag, normal cooldown/respawn-near/wipe/fallback, late join, native co-op actions/outlines/names, autosaves, dialect-discriminated v5/v6/v7 saves/pending clients/hubs/dead inventory; passive serialization and optional QBJ3 limbo/color/model cleanup with cancellation. Revival excluded. |
| QuakeC/mod compatibility | Both VM permission/slot/name/capability/argument/return/error contracts; actual program execution through native registries, strings/buffers/files/search/named calls/reflection/model and entity queries/message reads/CSQC drawing/input/events/localization; simultaneous VM resource ownership and reload/abort lifetimes. Preserve deliberate native data-path/capacity policies. Literal registration coverage alone is not proof. |
| Assets, maps and performance mechanisms | Actual PNG/TGA/JPG/JPEG/fullbright MDL/MD3/MD5/static/level loads, WAD3 per-texture palettes, native liquid lightmaps and CPU/GPU lightstyle interpolation, material/texture-cache/recolor identity, precision/clip bias; native worker rendering/loading, large-BSP/no-VIS/visibility/allocation behavior, alias/BLAS batching and scripted particle lifetimes/effects. Source mechanisms are required; measured speedups are not claimed. |
| Audio and discovery/UI | Actual optional Opus packet/jitter/generation/levels/mute owners and budgets, game/voice HRTF/native fallback/radio/occlusion/reverb/local wet reflections; system-default mic, saved VR default-on opt-out versus desktop opt-in; capture error/profile transitions, meter/HUD/PTT. Steam/rerelease/localization paths, mod launch/filter/catalogue/approval/validated package cancellation, reconnect and UI scale/branding. |

The native networking reuse checkpoint additionally requires
isolated IPv4/IPv6 connections through the real driver/connection owners:
explicit/default ports, bracketed literals, invalid names, unavailable IPv6,
`-noudp6`, cancel/reconnect and native socket cleanup. Merely inspecting the
driver table or testing loopback does not qualify these paths. Windows checks
remain deferred.

The newer-primary [model-path fallback](model-path-whitespace-2.0-plan.md),
source-integrated c034a2f5, needs actual native virtual filesystem/cache loading
with legitimate exact trailing-space filenames, absent-exact/existing-trimmed
names, missing both, space/tab/length/empty/inline/repeated-load cases and ordinary/
dynamic server-client precaches. Keep network and QC names unchanged; source
copying and a path-existence helper alone do not prove real loading/cancellation.

The [final C02 publication plan](metadata-publication-integration-2.0-plan.md)
adds complete native signon and metadata consumer cases. Existing
local_load_native_fixture.c prepares renderer signon and clears the peer's
reliable bytes before issuing spawn/begin; initial_state_native_fixture.c also
prepares initial transport/signon. Their later movement results cannot certify
the new pre-signon2/serverinfo or pre-signon3/all-slot drain. Reuse their engine/
loopback bootstrap, but exercise actual native sender, queued-byte draining,
client parser and CL_SignonReply/CL_SendCmd without manually skipping those
phases. Include empty and updates-only initialization; initial client aggregate
above1024; individual token/text and logical NQ/Fitz/RMQ limits; reconnect/map
downgrade; all16 slots across reliable sends; both profiles; mid-signon mutation;
spawn/fastload clears; retired/reused occupants; private/star/quote-invalid fields;
ordered near-full name/color overlays and actual movement/skin/scoreboard state.
The existing serverinfo_command_smoke.gdb isolates server command readers and
does not qualify this sender/lifecycle contract. Inspect live cvar/control
publication too; any connected unhandled failure remains a C02 finding.

The [current final-checklist Astra review](final-checklist-current-2.0-review.md)
adds explicit native control/wrapper cases within that same C02 family:
blocked reliable sends where the name reply fits but leaves fewer than ten bytes
for prespawn, and full-buffer begin; preserve CSQC loading/name/control order and
eventual complete admission without duplicate loading or partial commands.
Exercise refused live seta on a non-archived USERINFO cvar: string, flags, default,
VM/store and queued bytes must remain unchanged. Include declared metadata support
without PREDINFO, ordinary public/private defaults, and slot occupation with QC
intercepting normal name commands. Actual near-full canonical userinfo must retain
all representable custom fields and agree with native scoreboard name/colors after
each ordered overlay; permanent incompatibility must use the recipient failure
path. No default-profile exclusion or reachable scratch overflow is established
merely by the reviewed source guard omissions.

Main source integration review adds three precise cases within the same C02
scope. First, native desktop demo signon must progress with locally unrepresentable
name/userinfo without invoking live reply serialization or disconnect; preserve
once-only CSQC loading and native loading completion. Second, the real prespawn
sender must publish initial serverinfo before2 while retaining player-slot
obligations for the post-spawn/QC table before3, avoiding duplicate initial tables.
Third, full and reconstructed wire userinfo must contain canonical decimal colors
and representable names, preserving custom fields. Actual parser/skin consumers
must never observe a spurious zero color caused by custom-only snapshots; a
quoted native name is restored by the binary companion. Existing native
companions are retained, so this is not an assertion of zero skin-handler calls.
Cover native remove-first live store refusal too: serialize the actual prospective
empty/deleted value while preserving native cvar assignment semantics, rather than
requiring requested and stored values to match. These are software correctness/
ordering cases, not performance measurements or added demo features.

Final C02 source integration (`f3727a10`) also requires actual native prespawn
sender cases where signon bytes plus signon2 exactly fill an empty recipient
envelope, where they temporarily cannot fit the remaining room, and where they
permanently exceed that recipient's limit. Exact fit must progress, temporary
pressure must drain/retry, and permanent incompatibility must fail visibly at
the native recipient owner. Keep native signon storage and QC writers; no
split-buffer rewrite or simulated-counter-only acceptance.

The [C19 refinement plan](portable-linux-staging-refinement-2.0-plan.md) adds
artifact cases on both native architectures from the same committed snapshot.
Stage/verify the actual final dependency closure, then relocate into a disposable
path and exercise the executable plus executable-side OpenXR alias. Both loader
load contexts must resolve the same bundled descendants using only their own
RUNPATHs, without a global LD_LIBRARY_PATH. Verify original receipts, native
inputs, source versions, exact deb payload/notice ownership, matching source
access, aliases, architecture and GLIBC<=2.39. Negative disposable copies must
refuse removed installed notices/ELFs even when their manifest entries are also
removed, altered original bytes/receipts, escaping aliases, host-SONAME filenames,
provider conflicts and unresolved dependencies. Package metadata inspection
alone does not qualify relocated execution or runtime/driver discovery. Retain
host OpenXR runtime/driver dependencies as explicit external prerequisites.

The [avatar muzzle-light adapter](avatar-muzzle-light-2.0-plan.md) additionally
requires real previous-render publication followed by native entity relink:
two independent tracked players, canonical and attached Gun versus Axe/native
custom gear, head-only tracking, dominant-hand and identity/generation changes,
new valid pose sequences, strict 0.25-second socket expiry and invalid/backwards
time. Exercise local first-color-pass pitch, scaled/rotated entities, map/client
free, failed/empty palette publication, tasks on/off and CPU/GPU lightmaps.
Include forced/teleported/backwards-time relink without a flash while rendering
is skipped, then a flash before any new preparation: the retired point must
remain unavailable. Native bright/dim/rocket precedence and ordinary desktop
fallback remain required. The subsequent
[shared alias-player root repair](alias-player-root-2.0-plan.md) requires repeated
MBOIT/wireframe/overlay and tracked body/prop TLAS consumers to leave the original
alias entity angles unchanged and derive the same root. Cover copied versus
message-angle branches, tagentity, EF_ROTATE, absent movement history, local and
remote identities and viewmodels. Native non-alias dispatch and the non-tracked
TLAS pitch convention stay unchanged. Rendered agreement remains unqualified.

The [optional VRIK/voice capability retry](optional-capability-retry-2.0-plan.md)
requires actual offer parsing, successive `CL_SendCmd` frames, native reliable
draining and server capability consumption. Cover full buffers, exact12/13-byte
capacity versus one byte short, one reply fitting while the other remains
pending, `NET_CanSendMessage` false, malformed/unsupported exact tokens and first
supported choices including pending2 followed by4. Pending offers must not
activate send/receive admission; successful complete enqueue retains native
unreliable-before-reliable ordering without a new ACK gate. Map/disconnect must
retire intent. Native desktop playback must latch without writes even when its
live buffer is unusable. Invalid/overflowed live buffers retain intent; this
repair does not claim a new general buffer-recovery implementation.

The [native mod entity consumer repairs](mod-entity-consumers-2.0-plan.md)
require actual CSQC VM creation/conversion, efrag collection and desktop/stereo
rendering for nonzero static translation/rotation with skin/frame/alpha/effects
retained. Exercise actual server `.modelflags` snapshot-to-client relink on
unflagged models, model-only/both/zero flags, all seven fallback branches,
scripted entity/model precedence, pause/teleport/reset and native rocket light.
No renderer replacement or new general drawflags behavior is implied by these
bounded repairs; broader alpha/OIT/effects remain their native qualifications.

The [native pipeline-cache adapter](native-pipeline-cache-2.0-plan.md) requires
actual base/alternative/compute creation and rendered output across desktop/
stereo, MSAA/AO/OIT/foveation variants and same-device restarts. Use disposable
write roots for missing/valid/wrong-device-or-UUID/corrupt/truncated/oversized
cache files, allocation/file/cache/snapshot/replace failures where meaningful
facilities exist, and shutdown with render resources absent but cache alive.
Incomplete data must not replace a valid file; failed initialization must not
retry each frame/rebuild. Driver pipeline identity and shared native camera
descriptors stay intact; no new timing measurement or async compiler gate.

The [native BSP preparation adapter](bsp-plane-preparation-2.0-plan.md) requires
actual BSP29/2PSB/BSP2/Valve/Quake64 and inline-brush loading, visibility,
collision/hull0, native particles and serial/worker geometry preparation. Cover
animated/zero-style dependency and dirty-lightmap masks on CPU/GPU paths and
reload. Disposable malformed face surfedge spans/plane/texinfo and all three
node-format plane references must refuse before pointer/polygon/worker use.
This narrow boundary is not full malformed-BSP validation. Exact-count plane
storage leaves separately padded SIMD and box-hull arrays intact; no measured
RSS/loading-time benefit follows from source integration alone.

The inherited aim source repairs add focused cases to the VR movement and
presentation row: deadzone 0/70/out-of-range/nonfinite and unchanged valid
fractional values; controller-to-head/mouse/blended history with unavailable
hands, rejected exits during HMD loss, locks, centerview and absolute/relative
corrections; no retired VR seed
on a desktop setting change; pitched/rolled native intermission entities with
one physical-head contribution and unchanged desktop cameras. Native co-op
results, deathmatch scores and finale use the existing tracked intermission
panel. CSQC score/death/intermission canvas placement is now source-integrated
through the [existing canvas adapter](csqc-score-panels-2.0-plan.md). Final
qualification must cover mixed native inventory/voice, registered callback
combinations, source clipping, independent framebuffer scaling and native
intermission fallback after QC errors; source acceptance is not rendered proof.

The optional inventory stat repair adds actual registered-QC/server/client/wheel
cases to prediction/multiplayer and wheel qualification: representable bit31
and upper unsigned32 masks, ordinary fractions/negative values, invalid or
wrong-type fields, valid zero versus moditems/items_dwell fallback, both items2
packing paths and unchanged native custom-stat precedence. Packed STAT_ITEMS
retains the low nine items2 bits; STAT_VR_ITEMS2 retains the normalized full mask.
Keep ordinary core field values valid: this is a bounded optional-field repair,
not a complete native numeric-conversion audit.

The [voice map-reset repair](voice-map-reset-2.0-plan.md) adds a real serverinfo
case to audio qualification: buffered/jittered speech, generation, talking,
per-player controls and transmit/capture continuity must reset on a connected
map change before client teardown, even if no new speech arrives. Saved VR
opt-out/default-device settings remain intact and negotiated voice can resume.
Exercise repeated clear/disconnect and the no-Opus/native-audio fallback as
additional coverage; do not substitute a direct helper call for serverinfo.

The [saved-entity reference repair](save-entity-reference-2.0-plan.md) adds
actual numbered native save restoration: empty backward-reference targets,
forward global/edict references, nonempty targets, sparse high slots, reserved
clients and fastload. Serialized blocks must determine liveness; allocation
after restore must observe unique FIFO membership and native reuse delays.
Negative/out-of-range reference refusal and ordinary initial-map/QC/console
assignment remain required. A direct parser fixture alone is supporting evidence;
no new save format or parser state machine is implied.

The [voice capture continuity adapter](voice-capture-continuity-2.0-plan.md)
requires real producer/queue/HUD consumers for SDL2/3 detected query/read/put
failure, raw/converted backlog and positive short reads. Prior LIVE/preroll/VAD/
encoder/self-feed state must retire; repeated drops before network drainage
retain the exact pending END marker, without re-aging or replay policy. Healthy
zero/sub-frame buffering does not reset state or close devices. Held PTT resumes
on valid data, key release during the existing10-second retry remains effective,
and saved microphone consent/default-device choices remain unchanged. Include
actual local wet-only/spatial reset and no-Opus coverage. This does not require a
new silent-device timeout or certify audible/headset quality.

The [explicit defaults adapter](vr-default-restoration-2.0-plan.md) requires
actual built-in pak/default-script and menu-reset execution, mod-provided script
completion, attached/unfocused/desktop-to-XR cases, nonempty native/custom
bindings, and deliberate later unbind/custom assignment after missing-only
filling. Also cover a detached explicit reset after prior VR use followed by
single-key NULL/empty assignment, unbindall including already-NULL keys, or a
menu action clear with no currently bound matching key, before the next sample.
Those deliberate edits remain excluded while other missing controls restore;
an entirely excluded restore is consumed once. A new explicit reset starts a
fresh restore. Repeated ordinary frames must not reapply defaults. Reuse current
startup missing-only policy for a deferred first sample; script text alone is
not packaged-control behavior or native key-release/neutral proof.

The [deliberate paused impulse adapter](paused-impulse-adapter-2.0-plan.md)
requires real console/mod alias projection through CSQC, private recording and
native server-QC consumption. Cover explicit pause and local console-only
suspension, console closure before the next server tick, an opening never
observed by the server, and an already-due native arrival gap after a prior
marker. Existing server phase/terminal/gap policy must remain authoritative.
Exercise positive versus zero markers, reserved seq0/1, current versus delayed
epochs, ordinary native authority without prediction permission, and successive
suspensions. Pre-pause ordinary input still retires; deliberate unrecorded
intent remains until admission, replacement, explicit zero or map/disconnect.
Preview/final sampling must not duplicate or prematurely consume it. CSQC zero/
replacement consumes the projected request once at eligible recording; replay
uses native command history, without post-filter injection or ACK-owned copies.
Cover reciprocal live loopback identity, disconnected/nonloopback peers and
remote/public native behavior; preserve held controls and ACK/reliable drainage.
No read-only predicate/helper fixture alone establishes eventual mod QC effects.

Read game assets only from the existing quakespasm_straight installation. Native
runners may link/copy needed data into disposable writable profiles; saves,
configs and generated QC programs belong there. The map/mod matrix includes
stock, AD/ad_tears, q30a1024 and ordinary QBJ3/Enyo/Dwell/Mjolnir, mj4m1,
tershib/shib1_drake, peril/tavistock, large BSP2/no-VIS and original-level
z-fighting examples. Record absent assets as missing evidence, never as success.
No Mjolnir dual-state extension, mod-by-mod ability framework or performance
benchmark is required.

## Exit and final review

Upstream rehearsal source evidence (2026-10-01, read-only git ls-remote plus
official GitHub commit/compare API): Novum/vkQuake HEAD is
0d8121387e4c988951d2d58952793fa2482ef290,36 commits ahead of the pinned base
4bc898f29073e8aa41069f0e79e3cb5a9eb73afa, with that base as merge base. The
cached web commit listing was older; use an immutable fetched object in the
later disposable rehearsal rather than treating the cached listing as HEAD.
No fetch/merge/rehearsal has been performed in the production checkout.

This is a substantive candidate: changes intersect SSAO code/shaders, native
signon/QC writers, save owners, input/UI, textures and renderer resources. In
particular [upstream signon splitting](https://github.com/Novum/vkQuake/commit/57aa6f69b1f192a3dec68e07cb7436e5d9519c77)
changes the native server signon storage and several QC writers, beyond C02's
seven-file publication slice; do not improvise that port inside the active
worker. [Current upstream low-quality AO](https://github.com/Novum/vkQuake/commit/0d8121387e4c988951d2d58952793fa2482ef290)
changes depth-aware half-resolution evaluation. Rehearsal must document actual
conflict/resolution owners and preserve the shared quality setting with native
desktop versus stereo VR implementations, rather than claiming maintainability
from a clean cherry-pick of an unrelated file. New upstream features are not
silently added to the185-row frozen migration scope. Record any required
compatibility defect against its existing acceptance owner and resolve it before
completion; production integration requires its own bounded plan.

Record exact source revision, build options, platform, selected cases, results
and coverage limits per requirement. Correct failures at native owners, then
rerun affected coverage; do not repeatedly broaden green testing without cause.
After the full pass, use the requested local Astra senior review on the verified
implementation/evidence brief, spot-check its findings and record dispositions.
If effective settings cannot be verified, label source advice rather than
certification. Resolve required findings before claiming implementation complete.

User live headset/eye tracking/multiplayer/performance trials and Windows builds
remain outside this pass. General live incompatible VkDevice/device-loss rebuild,
skyrooms, quad views, Gorilla/swim propulsion, instant stop, physical-contact
melee/parry, Mjolnir hybrids, imagedump, revival, VR demos and additional demo
features do not become hidden gates.
Within surviving software scope, missing/indirect evidence remains open. Keep
the full migration goal active until its completion audit is supported.
