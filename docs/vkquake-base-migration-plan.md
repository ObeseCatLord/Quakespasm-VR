# vkQuake base migration: comparison and preservation plan

Reviewed plan, 2026-09-20. Scope is **all work since the inherited
QuakeSpasm-OpenVR base**, inherited VR behavior itself, current uncommitted work,
and the OpenXR/Vulkan/performance goals. This document does not execute a rebase
or declare a migration successful. The user subsequently authorized implementation
on `2.0`; see [migration status](migration-2.0.md) for the current checkpoint.

The original evidence table below records the architecture review snapshot. The [current feature map](migration-feature-map.md) supersedes its source-scope counts and adds newer master fixes, detailed migration owners, acceptance cases, and optional Ironwail/QSS-M candidates. The actual working branch is `2.0`.

## Evidence and limits

| Item | Verified state |
|---|---|
| Inherited reference | `8c5a6007a60098b6a5b5c5b552def70e1238a852` (2024-08-04), the merge base of this fork's `master` and the locally recorded gameflorist/hiina upstream branches |
| First first-parent project change | `68992122`, 2026-02-25, initial project bindings/defaults |
| Product reference | `master` at `7bc466b594e7a7e584dc47879eb6c00f971b01b1` |
| Current development reference | `openxr` at `969e6b33571985b901c920d6555ee8247437341b`, plus explicitly preserved working-tree changes |
| Whole inherited-to-product delta | 488 reachable commits (468 first-parent), 454 changed paths; 182 changed C/C++ source/header paths under `Quake/` |
| OpenXR branch delta | 42 commits, 145 paths, 37,311 added / 1,480 removed lines relative to product reference; these figures include supporting artifacts |
| vkQuake reference | `4bc898f29073e8aa41069f0e79e3cb5a9eb73afa`; remote HEAD rechecked 2026-09-20 |
| Ironwail reference | `08d578136ff43d7d1ef38e636dfbfd3e844be7cd`, a technique donor, not another engine base |
| Git ancestry | Both repositories are non-shallow. Read-only cross-object `git merge-base` finds no common commit between product reference and vkQuake reference |
| Qualification | Current OpenXR/GL has recorded software and native Windows checkpoints. Recent native Vulkan scene/stereo/lighting implementation is unbuilt/unvalidated. No complete native VR performance comparison exists |

Counts establish scope, not effort or completion. Imported code, vendored headers,
reverts, old build artifacts and formatting inflate diffs. Preserve the final
behavior, not every intermediate commit or abandoned implementation. Reviewing
the history and file inventory is not a claim that every behavior has been tested.
For example, 178 of the 454 paths are under `MacOSX/`; they are not 178 active
VR features. The two committed change sets cover 552 distinct paths before WIP.

Load-bearing source evidence:

- Current `Quake/protocol.h:51` declares the fork's movement protocol, including
  `PEXT2_EXPLICITCMDMSEC`; `cl_main.c:1930` implements prediction and
  `pmove.c:2166` supplies shared movement. Current `sv_user.c` consumes the
  sequenced movement/VR extensions. vkQuake `protocol.h:51` advertises a narrower
  replacement-delta/predinfo set; its `sv_user.c:474` and `cl_input.c:537` are
  not this fork's complete PMove and VR command path.
- Current `r_alias.c`, `r_vrik.c`, `r_avatar.c`, `gl_model.c` and `custom_avatar.c`
  jointly own live posing, retargeting, asset validation and attached equipment.
  This work cannot be preserved merely by copying `vr.c`.
- Current `world.c:2492`, `sv_phys.c`, `host_cmd.c:1429`, `pr_cmds.c` and
  `pr_edict.c` contain co-op touch/inventory, melee, save and QuakeC behavior.
- vkQuake `gl_rmain.c:1522` contains the render task graph, including particles,
  world drawing, lightmaps and acceleration structures. `gl_screen.c:1579`
  connects those tasks to frame lifetime. `r_brush.c:3590` coordinates GPU
  lightmaps/indirect generation, and `gl_model.c` has texture/skin loading tasks.
  These are working upstream systems to preserve as a unit when practical.
- vkQuake `gl_model.h:374` embeds Vulkan model buffers/joint resources; its model
  and texture lifetime differs from this fork's hunk/cache arrangement. A whole
  renderer transplant has a significant loader/representation boundary too.
- vkQuake `gl_rmain.c:385` uses one desktop view's globals; no OpenXR/OpenVR or
  multiview implementation was found in the pinned engine/shaders. Two calls to
  its desktop render function do not establish correct or efficient VR.
- vkQuake Meson uses SDL3 on Windows (`meson.build:78`); this fork uses SDL2.
  Input/audio/platform integration is part of migration, not a build-file rename.
- vkQuake's Meson project currently declares C only (`meson.build:1`). Current
  VR/OpenXR modules compile as C++. Add C++ only for those modules and their
  link requirements; do not convert the donor's whole C renderer to C++.

## Which direction is faster or better?

There are three real options. A literal replay of all old commits is not a
fourth useful design: the histories and architecture differ, and many commits
fix or revert earlier intermediate states.

| Direction | What it preserves/inherits | Major remaining cost | Assessment |
|---|---|---|---|
| A. Complete current incremental native Vulkan renderer | Retains all current gameplay/network/model policy and existing XR integration in place | Finish donor GPU batching, threading, lighting/shadows, precision/MSAA and parity; maintain a renderer distinct from upstream | Lowest disruption to existing product behavior; full-goal time and future maintenance remain uncertain |
| B. Retain current engine core, consolidate a cohesive vkQuake rendering/loading subsystem | Preserves gameplay/networking; reuses donor render scheduling, GPU structures and effects together | Reconcile model/texture/particle representation, avatar posing and stereo; potential adapters across tightly coupled loaders | Credible middle option, but not automatically smaller than a donor-base migration |
| C. Start a maintained branch from vkQuake, migrate the complete product behavior | Inherits donor renderer, task system, assets and future upstream ancestry | Migrate all inherited VR and product changes, reconcile prediction/VM/saves/models/platforms, then integrate XR/stereo/foveation | Strongest direct upstream alignment; largest initial behavior-preservation surface |

**Recommendation after senior review: C, with the current gameplay core retained
where it owns product behavior. Confidence is moderate.** Start from vkQuake's
ancestry and preserve its cohesive rendering/loading/resource/task implementation.
Port the fork's existing movement, protocol, co-op, saves, VR and mod policies as
coherent implementations, adapting their engine access where necessary.

For the complete requested goals and future upstream merges, this is the best
expected direction. It is **not demonstrated faster** to regain all product
behavior. A may reach a near-term milestone sooner; C avoids independently
reconstructing more of the donor renderer and offers the strongest maintenance
starting point. Runtime performance, latency and delivery time remain unproven.

B and C partly address different axes: subsystem ownership and Git ancestry.
A donor-based branch can retain the current gameplay algorithms. If B also keeps
donor ancestry and cohesive donor ownership, it converges on the recommended C.
There is no need to replace working gameplay algorithms merely to use that base.

The earlier blanket rejection of a broad base change is superseded for this
planning decision. Continuing A merely because it already has many commits is
not justified. The task system, GPU world generation, lightmap layout and
ray-query pipeline are integrated upstream; copying them one feature at a time
risks maintaining a second implementation indefinitely. Conversely, an adapter
is not economical merely because it is called an adapter. The early proofs below
must establish that C can preserve behavior without recreating either engine.

No calendar estimate, completion percentage or measured speedup follows from
this review. Migration remains conditional on the bounded integration proof;
the planning review itself made no engine changes. Implementation now proceeds
on the separately created `2.0` branch, subject to these proof gates.

## Complete preservation ledger

These are work packages, not permission to drop anything outside the headline
list. Before migration, every changed path and surviving behavior since the
inherited baseline must map to a row and a disposition. Include inherited VR
sources which might not appear in a product-only diff. Record original symbols,
representative commits, donor equivalent, destination, acceptance case and
status: direct reuse, adapted port, donor-equivalent verified, or unresolved.
Never equate a matching feature name with behavioral equivalence.

| Package | Behavior and current owners | Migration treatment / acceptance |
|---|---|---|
| Inherited VR | `vr.c`, `vr.h`, `vr_menu.c`, `view.c`, input/screen hooks; poses, controllers, room scale, turning, aiming, haptics, HUD/menu placement, mirror, loading and focus behavior | Preserve final behavior and settings; port pose/input/math independently of GL commands. Check tracking loss, recenter, hands, weapon alignment and loading transitions |
| Wheel and weapon configuration | Wheel/catalog/hit helpers, `vr.c`, `sbar.c`, menu/client input; desktop and playspace modes, ownership stats, mod profiles, offsets, haptics, co-op actions | Reuse selection/profile logic; adapt drawing to donor contexts. Verify custom weapon bits, hover, mod switches, impulses and exact muzzle/grip transforms |
| Prediction and transport | `pmove.*`, `cl_input.c`, `cl_main.c`, `cl_parse.c`, `sv_user.c`, `sv_main.c`, `net_*`, `protocol.h`, host pacing | Treat as a coupled subsystem. Preserve command timing, redundancy/ACK baselines, split snapshots, packet budgets, NAT/reconnect, prediction and QC ownership. Do not substitute donor PREDINFO support for parity |
| Co-op and persistent state | `world.c`, `sv_phys.c`, `host_cmd.c`, `coop_inventory_policy.h`, server/client state | Shared pickups/keys, respawn/teleport, outlines, classic-co-op option, dead-player inventory, late joins, hub progression and multiplayer saves; co-op revival is user-excluded. Use copied saves in acceptance, never overwrite deployed saves |
| Physical VR gameplay | Melee adapters, `vr_gorilla*`, movement/server physics/contact protocol, akimbo/model splitting | Preserve authority, continuity, replay and teleport/reset semantics, mod-specific weapon contracts, platforms/ladders/swimming, desktop coexistence; no new parallel movement protocol |
| Avatar/VRIK/FBT | `r_alias.c`, `r_vrik.*`, `r_avatar.*`, `vrik_codec.*`, `vr_fbt*`, `player_avatar.*`, `custom_avatar.*`, `gl_model.*`, package assets/docs | Reuse rig solving, validation, calibration/storage, identity negotiation and pose transport. Feed actual posed geometry to donor draw/shadow paths; cover MDL/MD3/MD5, attached props, remote/dead/desktop and nonhumanoid variants |
| QuakeC and mod compatibility | `pr_*`, `progs.h`, `common.c`, CSQC paths, HUD/input and model/texture lookup | Diff builtin numbers/contracts and fallback behavior against donor; preserve implemented rerelease/mod fixes, custom HUDs, input callbacks, localization and file/asset precedence. Skyrooms are outside the 2.0 goal. |
| Particles, weather and audio | `r_part*`, `cl_tent.c`, `snd_*`, `voice*`, audio controls | Prefer donor particle scheduling with preserved effect semantics and once-per-frame simulation; preserve VOIP consent/device policy/jitter/spatial behavior and optional Steam Audio. Adapt SDL APIs without changing settings |
| Mod discovery and delivery | `addon_catalog.*`, menu/browser, Steam/rerelease discovery, download/reconnect helpers | Preserve catalog, installed-mod selection, server-required downloads, path priority, branding and configuration defaults |
| Existing GL performance work | Material buckets, static indices, no-VIS/stereo visibility, alias instancing, GPU MD3/MD5, hidden masks, optional mirror and diagnostics | Preserve benefits or map to demonstrably equivalent/better donor mechanisms. Do not stack duplicate caches or port GL API code into Vulkan; retain old executable as behavior/performance reference |
| New OpenXR work | `vr_openxr*`, foveation math/policy, XR input/tracker/session code and fixtures | Reuse common runtime logic and tests. Replace renderer attachment calls only where donor ownership requires it. Runtime-selected Vulkan device/queue and image lifetime remain mandatory |
| Single-pass stereo and foveation goals | Existing stereo eligibility/camera work, Vulkan shaders/pass experiments and current constraints | Adapt donor view/task/pass data for two views; preserve per-eye visibility and transparency. Eye tracking optional/off; lost gaze full quality; fixed explicit-only. Qualify KHR/FDM by runtime/GPU, not headset name |
| New large-map/feature goals | Donor feature audit; compact marksurfaces, memory/loading changes, precision/bias, GPU lighting, threading, Ironwail techniques | Inherit donor equivalents instead of re-porting them; carry only proven missing improvements. Preserve dark-area precision and no-z-fighting goals; expose useful diagnostics for the user's later `mj4m1` measurements. |
| Builds, assets and operations | Native Windows, Linux x86-64/ARM64, loader/licenses, `quakespasm.pak`, CI, private builders/deployment, tests | Retain client/catalog/audio features, ABI/package contracts, config/save paths and dedicated compatibility. Migrate private tools locally without publishing them. No deployment during research |
| Uncommitted and later-discovered behavior | Current movement/Gorilla/avatar edits and pending lightmap patch | Preserve separately, identify author intent and validation state. Known pending GPU-history/late-overbright issue is not accepted behavior to reproduce. Any unmapped path or feature remains an open migration item |

## Plan C: migrate onto vkQuake without making upstream unmergeable

### 0. Preserve references and inventory before editing

Create a separate migration worktree/branch rooted in the pinned vkQuake commit,
now created as `2.0`. Keep `master` and `openxr` histories and the dirty
working tree intact. Archive or commit owned WIP separately with an explicit
manifest; do not silently bundle unrelated edits into a migration checkpoint.
Record the complete inherited-to-product, product-to-openxr and WIP ledgers.
Keep existing behavior/reference binaries and test fixtures available.

Use final-state, topic-sized ports with source-commit provenance, not a blind
488-commit replay. Final fixes supersede reverted experiments; preserve authorship
and licenses for reused code. Retain original history through existing branches.
Do not merge unrelated full trees just to manufacture ancestry.

### 1. Prove the expensive seams before migrating the whole catalogue

Use donor Vulkan/device/render/resource/task owners. Freeze a specific reference
client/server revision and its protocol configuration; do not assume `master`,
`openxr` and working-tree variants are interchangeable peers.

The smallest first slice is one migrated client, one real map, tracked head and
held weapon, correct asymmetric eye views, and authoritative movement/fire
against that reference server. Reuse the coupled command/prediction/host cadence
needed for this slice. Serial rendering may establish initial correctness.

Before mass feature migration, extend the slice to demonstrate:

- Donor tasks enabled and a representative opaque multiview path, with one
  simulation/particle/HUD update per logical frame.
- Two instances of the same MD5/custom-avatar asset with different tracked poses
  and attached equipment; retain rig data and provide one prepared pose per
  entity to both raster and animated shadow geometry.
- A real moving brush with lighting, and clean map/session teardown while work
  is in flight. A static-world screenshot does not qualify these boundaries.
- Separate completion of CPU recording, actual queue submission, XR image
  release and GPU retirement. Donor `SCR_UpdateScreen` joins `draw_done_task`
  while `end_rendering_task` remains pending and performs the later submission.
  Do not release an XR image just because scene-recording jobs have joined.

Record touched upstream files, code reused versus rewritten, semantic conflicts,
task joins and duplicated owners. Reopen A/B/C if success requires a second model
manager/render graph/movement authority, per-eye simulation, replacing established
gameplay semantics, or persistent global serialization that defeats donor tasks.
Also reopen if each repaired seam demands more cross-subsystem machinery. Do not
call a sprawling adapter a thin layer. These are future acceptance gates; no
integration test or engine build was run during this planning task.

### 2. Establish the product core on the donor

Port networking/prediction/server physics/QuakeC/save compatibility as coherent
work packages with their existing owners. Compare donor fixes in overlapping
functions instead of overwriting entire files with the older fork. Preserve
protocol numbers, negotiated capabilities and format invariants; any unavoidable
wire/save change is an explicit migration issue, not a silent cleanup.

Preserve host cadence and the execution thread for gameplay/QC. This requires an
explicit scheduling change: donor `SCR_DrawGUI` is a task, and its HUD can execute
CSQC under a mutex. Run the mutable QC/HUD/pose preparation on its established
owner thread, integrating its drawing with donor contexts and task dependencies.
Do not introduce a second VM or a generic snapshot of the entire engine. A mutex
alone does not establish compatibility with this fork's callbacks and cvar changes.

Save compatibility needs explicit dialect handling before restoration. The fork
reads versions 5/6/7 and writes 7; its version 6 is multiplayer. vkQuake's version
6 is KEX and can trigger a game switch while loading. Recognize and validate the
candidate dialect in read-only input before changing game/server state. Validate
the required record structure, not just the version or a plausible next token;
reject ambiguous/malformed input before mutation. Feed accepted data into one
restoration owner, retaining fork multiplayer semantics and donor KEX support.
Do not renumber or overwrite existing saves. A version-7 writer remains compatible
with the product until any intentional format change is separately resolved.

Establish candidate-server interoperability in both directions with the frozen
reference pair, then co-op pickups, death/rejoin, map/hub transitions and copied
save restoration before catalogue-wide UI/audio migration. Include both version-6
dialects, version-7 dead-player inventory and pending/late-joining players in
acceptance. Unknown wire/save differences remain explicit migration blockers.

### 3. Complete VR/product behavior

Bring across wheel/calibration/menu/input, physical interactions, all avatar/FBT
packages, voice/audio and mod-delivery behavior. Reuse existing code where its
contract is unchanged. Add small donor draw/context hooks for prepared geometry
and material data; do not copy the current monolithic `r_alias.c` over the donor
or introduce a second asset/model manager.

Preserve inherited VR behavior through the OpenXR adapter, including poses,
controller bindings, haptics, menus, avatars and session continuity. The later
[feature-map runtime decision](migration-feature-map.md#implementation-order-and-architecture)
sets OpenXR and desktop as release targets; a second OpenVR Vulkan compositor
is not required. Keep the old `openvr.h` and OpenGL transport in the source
reference for behavioral comparison, and migrate assets/defaults that still
serve the OpenXR product. Ordinary desktop and non-eye-tracked VR must continue
to work.

### 4. Integrate stereo and optional effects within donor ownership

Extend the donor's existing camera/task/pass structures to carry the prepared
stereo pair and per-eye eligibility. Keep one simulation, one animation/light
preparation owner and one XR submission owner. Share opaque work where valid;
retain correct per-eye ordering for transparency, water, particles, weapons and
UI. Preserve actual runtime image formats, array layouts, clears, masks and
session-loss behavior. No second render graph beside the donor's graph.

Use donor GPU lightmaps, indirect drawing, multithreaded loading/rendering,
MSAA/color pipeline and optional ray-query effects as the starting implementation.
Adapt moving receivers/casters and actual custom-avatar poses; stereo shadow
coverage cannot use one desktop camera's visibility. Extra effects are optional,
not presumed performance improvements. Reuse current foveation policy/math and
proven lifecycle pieces; do not transplant the entire experimental renderer.

Keep fixed foveation off unless explicitly selected, expose eye tracking as an
independent VR toggle, and render at full quality when gaze is invalid or
unavailable. Preserve full-eye geometry visibility
and protected-content depth. Steam Frame streaming and standalone, Beyond 2e on
Linux/Monado, Windows and other capable headsets remain separate qualification
cases; donor migration does not supply gaze-provider/runtime support.

Valve's [Steam Frame custom-engine guidance](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
recommends Linux ARM64 for native builds and documents
`XR_VALVE_frame_controller_interaction`, `XR_EXT_eye_gaze_interaction`, and
the `XR_FB_*`/`XR_META_foveation_eye_tracked` foveation path. The current
OpenXR adapter requests those advertised extensions; extension presence still
does not prove active gaze, usable rates, or standalone frame time.

### 5. Carry only missing large-map improvements

For each Ironwail/current-fork improvement, first inspect the chosen donor version.
Compact marksurfaces, dynamic memory, SIMD/GPU visibility, batch assembly, partial
updates and asynchronous resource patterns must have one owner each. Retain
current no-VIS/stereo correctness fixes where donor desktop assumptions differ.
Avoid importing two implementations of the same optimization.

The user will measure performance after implementation. Keep diagnostics and
equivalent graphics/settings available so the old product, unmodified donor
desktop and migrated build can later be compared on `mj4m1` and smaller scenes.
Comparative frame-time, loading and memory results are not a 2.0 implementation
completion gate and no speedup is claimed before those measurements. Optional
dynamic resolution is a separate user feature.

### 6. Qualify the complete release and switch only after parity

Run accumulated relevant fixtures and end-to-end checks after integrated slices,
not a full build after every edit. A bounded early feasibility experiment is
needed to decide the architecture; actual hardware/gaze tests remain with the
user later. Validate native Windows, Linux x86-64 and native ARM64 client builds,
not a dedicated-only ARM substitute. Exercise old config/save migration, two-client
co-op, reconnect/map changes, voice, avatars and mod downloads.

No main-branch replacement, release, deployment or force-push is part of this
plan. Promote the new line only after the preservation ledger has no unexplained
omissions and the relevant end-to-end behavior is demonstrated.

## Upstream maintenance model

- Give the new branch real vkQuake ancestry. Track the untouched upstream in a
  dedicated remote, such as `vkquake-upstream`; do not repoint the product's
  existing `origin`/inherited `upstream` remotes accidentally.
- Keep stable product history. Merge later vkQuake revisions into an integration
  branch and promote reviewed merges; rebase only unpublished topic branches.
  This supports ordinary three-way merging without rewriting published VR work.
- Keep donor file names, formatting and subsystem ownership where possible.
  Put truly VR-specific math, codecs, profiles and policy in dedicated modules;
  keep changes to donor files as named integration points. This is an ownership
  rule, not a promise that shared-file changes will be tiny.
- Maintain a short patch inventory: purpose, touched donor files, behavior owner,
  upstream source, conflicts and acceptance. Each imported improvement records
  donor SHA/license. Each original behavior records its old source commits and
  destination. Upstream-equivalent code should be removed only after equivalence
  is verified; do not keep a dormant duplicate indefinitely.
- Map expected conflict zones explicitly: frame/view/task scheduling, model/texture
  lifetimes, client/server/protocol, QC/save paths, SDL input/audio and build files.
  Network and co-op changes are substantial unavoidable divergence. An upstream
  merge applying cleanly is not semantic compatibility.
- On each upstream update, inspect changes in those zones even if Git reports no
  conflict; run their targeted integration cases. Preserve upstream bug fixes
  alongside product extensions. Avoid sweeping renames, formatting, generated
  output and broad version-conditionals that obscure meaningful diffs.
- Propose broadly useful fixes upstream only with separate explicit authorization
  to communicate/publish. Keeping local patches focused helps even without that.
- Keep private deployment helpers, credentials, runtime data and mods out of the
  public tree. Version source/build/package contracts without coupling upstream
  merges to live-server deployment.

Acceptance of maintainability requires an upstream-merge rehearsal against a
real subsequent donor change, covering at least an affected renderer/loader area.
A no-conflict formatting-only merge is not persuasive evidence. If no suitable
new change exists at the checkpoint, mark this part unverified rather than
manufacturing a success. Measure conflict scope and semantic review work; there
is no credible guarantee of conflict-free upstream updates.

## Senior-review disposition

Astra reviewed the completed draft using `gpt-6-astra` and explicit `xhigh`.
The main agent separately verified both configured and effective model/effort
metadata; the reviewer could not inspect those settings through its interface.
The main agent spot-checked save constants/parser order, pending end-render task
and queue submit, live-pose sources, CSQC task execution and input cadence.
No builds, runtime tests or exhaustive per-behavior qualification were performed.

| Recommendation | Disposition and plan change |
|---|---|
| Prefer C while retaining product gameplay implementations | Adopted conditionally. Donor ancestry/rendering owners and reused product policies form the recommended architecture; early integration must validate the boundary |
| Treat B and C as potentially convergent, not wholly different engine designs | Adopted. Repository base does not dictate replacing gameplay algorithms; avoid paying for both a donor import framework and a new engine |
| Address colliding save version 6 explicitly | Adopted. Added read-only dialect recognition/validation before any mod switch or restore, and copied-save cases for both dialects |
| Separate task completion from actual XR submission/release | Adopted. Added explicit recording, submission, release and retirement boundaries to the first architecture proof |
| Prove donor tasks and multiview before mass migration | Adopted. Serial rendering is only initial evidence; same-asset independent avatar poses, moving brushes and once-per-frame updates expose the hard seams |
| Retain live rig data and share per-entity pose between draw and shadow paths | Adopted. Extend donor model data/lifetime rather than duplicating model ownership or merely adding a draw callback |
| Specify CSQC scheduling and retain prediction/host cadence together | Adopted. Added the donor worker-HUD incompatibility and a narrow owner-thread adaptation; pinned interoperability references are required |
| Make inherited OpenVR preservation unconditional | Adopted. Included Vulkan compositor work and unchanged inherited dependencies/assets/defaults in the ledger |
| Do not infer speed or maintainability from clean merges or file counts | Adopted. Performance comparisons and a substantive upstream merge rehearsal remain qualification requirements |

The OpenVR row records that review's historical disposition. The later user
release target supersedes its runtime-backend requirement: preserve the
behaviors through OpenXR, without implementing a second compositor. See
[runtime migration qualification](migration-feature-map.md#implementation-order-and-architecture).

Outstanding uncertainties are adapter size, full ledger equivalence, measured
performance and runtime/device qualification. They require implementation evidence,
not a decision to discard features. Existing source references and the plan are
preserved; actual migration is not performed by this document.

## Official and primary references

- [vkQuake source and feature documentation](https://github.com/Novum/vkQuake)
- [Pinned donor render/task entry](https://github.com/Novum/vkQuake/blob/4bc898f29073e8aa41069f0e79e3cb5a9eb73afa/Quake/gl_rmain.c)
- [Pinned donor model/resource structures](https://github.com/Novum/vkQuake/blob/4bc898f29073e8aa41069f0e79e3cb5a9eb73afa/Quake/gl_model.h)
- [Pinned donor protocol](https://github.com/Novum/vkQuake/blob/4bc898f29073e8aa41069f0e79e3cb5a9eb73afa/Quake/protocol.h)
- [Khronos OpenXR Vulkan enable2 contract](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable2.adoc)
- [Official Git rebase semantics](https://git-scm.com/docs/git-rebase)
- [Official Git merge semantics](https://git-scm.com/docs/git-merge)

The supplied Skyrim VR architecture postmortem was reread for this comparison:
reuse mature behavior, isolate demonstrated incompatibilities, test necessary
boundaries, and reopen the decision if adapters become a parallel engine. That
principle applies equally to retaining current product gameplay and retaining
vkQuake's mature renderer; it does not predetermine which tree must be the base.
