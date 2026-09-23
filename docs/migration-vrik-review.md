# VRIK transport and Vulkan avatar adapter: senior review brief

Status: the v2/v3 codec, optional server/client transport, OpenXR head/hand
sender, and transport review fixes are committed in `f8ed9a69`, `e182a773`,
`bb8ca4c5`, and `275b94c2`. The first Vulkan Ranger draw adapter is
committed in `7580b1c0` and `54e9b3c3`; matching ray shadows, conservative
tracked bounds, and live cross-play remain unfinished. Focused local Astra reviews cover sender, Vulkan avatar
architecture, and transport. The
release goal is a single vkQuake-based Windows/Linux/ARM desktop and OpenXR
engine where desktop and VR peers share gameplay and can see the VR peer's
tracked avatar when available. A peer without tracking keeps ordinary player
animation. This is a solo-maintainer port, not a new networking or rendering
platform.

## Checked evidence

| Claim | Status and source |
| --- | --- |
| `2.0` has the donor VRIK codec, negotiated transport, OpenXR head/hand sender, and a first visible Vulkan Ranger draw adapter. | **Verified:** `f8ed9a69` imports `Quake/vrik_codec.[ch]`; `e182a773` adds transport; `bb8ca4c5` adds the sender; `7580b1c0` binds frame-owned MD5 palettes. Ray shadows and live behavior remain unverified. |
| Inherited transport uses v2/v3 bodies and explicit per-peer capability. | **Verified:** donor `Quake/vrik_codec.[ch]`; `protocol.h` defines `clc_vrikpose=5`, `svc_vrikpose=87`; `cl_parse.c:CL_OfferVRIKProtocol`, `sv_user.c:SV_ReadVRIKPose`, `sv_main.c:SVFTE_AppendPendingVRIK`. Donor server advertises through comment stufftext, relays only to capable recipients, and records sequence/generation. |
| Target opcode numbers 5 and 87 are reserved for negotiated VRIK messages. | **Verified:** `Quake/protocol.h` declares `clc_vrikpose=5` and `svc_vrikpose=87` in `e182a773`. This does not establish compatibility with arbitrary external protocol dialects. |
| A VR muzzle pose reaches the authoritative server, and tracked head/hand poses have a VRIK sender, relay, and visible MD5 draw adapter; live appearance is unverified. | **Verified:** `Quake/vr_input.c:VR_InputPreparePrivatePose`, `Quake/sv_phys.c:SV_BeginPrivateVRWeaponPose`, `e182a773` transport, `bb8ca4c5` sender, and `7580b1c0` draw adapter. The private QSVR profile remains per client and server-default off (`Quake/sv_main.c:SV_SendServerinfo`). |
| Donor reusable CPU code exists beyond the wire codec. | **Verified:** donor `r_vrik.c` is rig/skinning math and cache logic (~940 lines); `player_avatar.c` is identity parsing (~374 lines). Its actual render/skin integration is in donor `r_alias.c` and `r_avatar.c`, while target `Quake/r_alias.c` already owns vkQuake MD5 GPU skinning and draw calls. |
| Normal MD5 skinning reads static model matrices; compatible tracked players bind a frame-owned palette through the same set-3 shader slot. | **Verified:** `Quake/r_alias.c:GL_DrawAliasFrame` selects the prepared descriptor and zero blend only for a matching tracked entity; otherwise it retains `paliashdr->joints_set` and ordinary animation. `Shaders/md5.vert` reads set-3 `joint_mats[]`. |
| Animated ray-shadow geometry has a separate pose consumer. | **Verified:** `Quake/r_brush.c:R_BuildTopLevelAccelerationStructure` calls `Quake/gl_mesh.c:R_UpdateAnimatedBLASes`, which recomputes per-entity skinned vertices for ray-query BLAS. A visible VRIK palette alone would leave shadow geometry stale. |
| Visible entity draw and BLAS build can run as separate tasks. | **Verified:** `Quake/gl_rmain.c:V_RenderView` submits `R_DrawEntitiesTask` and `R_BuildTopLevelAccelerationStructure` after frame/efrag prerequisites, without an edge between them. A live avatar palette must be prepared once before both consumers or otherwise synchronized; lazy mutation from either worker would race. |
| A descriptor bound for a pending draw cannot be overwritten or freed at will. | **Verified from [Khronos Vulkan descriptor-set specification](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html):** descriptor contents must remain valid through their GPU use. Target `R_AllocateDescriptorSet` uses the shared persistent pool, so per-draw allocation without a retirement owner would leak; updating one in-flight set would be invalid. |
| OpenXR tracker enumeration already exists in the target runtime boundary. | **Verified:** target `Quake/vr_openxr.cpp` discovers HTCX/MNDX tracker devices and publishes them in `vrxr_frame_t`; mapping them to VRIK hip/feet and avatar state remains unimplemented. |
| Exact cost of adapting donor IK to the target MD5 palette and preserving GPU skinning is known. | **Unknown.** Requires targeted layout/task/animation inspection and a vertical proof; no proof from matching MD5 names alone. |
| An older/noncapable peer can safely receive the private VRIK opcode. | **Contradicted by protocol:** it must never receive that opcode; ordinary entity animation remains the fallback. |
| Public desktop play and private predictive play are independently selectable per peer. | **Verified:** `Quake/sv_main.c:SV_SendServerinfo` admits `QSVR_PROTOCOL_PINNED` only for a qualifying client when `sv_qsvr_private` is enabled; its default is `0`. `Quake/cl_input.c:CL_SendMove` retains separate public and private send paths. This is a code boundary, not proof of live cross-play. |
| Donor and target differ at the client stufftext and pose-source boundaries. | **Verified:** donor `cl_parse.c:CL_ParseStuffText` buffers newline-terminated extension commands and donor `vr.c:VR_GetVRIKPose` samples OpenVR. Target `cl_parse.c:CL_ParseServerMessage` dispatches a complete `svc_stufftext` string immediately, and target `vr_input.c` consumes `vrxr_frame_t` from OpenXR. Port the canonical offer parser and pose conversion at these target boundaries rather than copying either donor entry point verbatim. |

## Architecture choice and open decisions

| Decision | Current lean and rejected alternative |
| --- | --- |
| Wire ownership | Reuse the now-imported donor `vrik_codec.[ch]` and v2/v3 framing with exact validation, sequence/generation, stale/inactive and map-transition resets. Put a small adapter at the existing `cl_input`/`sv_user`/`sv_main`/`cl_parse` owners. Reject a second movement stream or embedding all poses in QSVR usercmds: VRIK must be independently optional for public desktop peers and existing servers. |
| Capability boundary | Keep VRIK opt-in per sender/recipient, independent of `sv_qsvr_private`. VR pose production requires tracking; desktop recipients may opt in to render a VR peer, while legacy peers receive only ordinary entities. Reject a global server dialect that would force desktop clients to parse VRIK. |
| Avatar rendering | Retain target entity selection, MD5 loader, Vulkan draw pipeline and multiview. Adapt donor CPU IK/skin helpers at the existing MD5 pose/palette boundary, falling back to normal animation for unsupported models or stale tracking. Reject transplanting donor OpenGL `r_alias.c`/`r_avatar.c`: it would duplicate/replace vkQuake's renderer and task policy. |
| Presentation scope | First prove inherited Ranger head/hands on a single compatible remote player, including both eye views and desktop view. Then bring avatar identities, lower body/FBT and custom avatars through the same boundary. Reject debug markers as a completion claim; they do not prove requested avatar behavior. |

Potential overlap: capability negotiation and avatar identity both use comment
stufftext. They may share parsing entry points, but pose protocol and user model
choice have distinct state. The reviewer may merge decisions if one state owner
can represent both without losing independently optional behavior.

The smallest end-to-end proof is a VR client sending a real head/hand sample to
a dedicated server, relaying only to an opted-in desktop client, then updating
that client's existing MD5 player draw and shadow pose in the same world. A
tracking gap, inactive packet, map change, entity-slot reuse, or non-MD5 model
must return to ordinary animation without replaying stale limbs. A legacy
desktop peer must connect and play without receiving an unknown opcode. Static
packet counters or codec fixtures alone do not satisfy this proof. Save full
headset/Windows/ARM verification for after implementation, as requested.

Review depth: challenge the adapter versus renderer transplant, the real
smallest vertical proof, protocol collision/opt-in assumptions, and task/GPU
pose ownership. Do not re-review OpenXR loader/foveation, general prediction,
the menu crosshair slice, or the whole migration plan. Rank the highest-risk
decision; flag concrete missing evidence and simplification opportunities.

## Focused Astra review disposition

A local Astra Max review verified the client sender boundaries and returned a
focused result before completing the requested renderer/transport critique.
Treat these findings as review of packet admission, not as sign-off on the
remaining architecture decisions.

| Verified finding | Disposition |
| --- | --- |
| Donor `CL_AppendVRIKPose` consumes sequence/timer/inactive state only after the full framed body fits; failed append leaves the packet intact. | **Adopt.** Encode first, check remaining bytes, then append and commit send state. A too-large pose retries with the newest tracking sample; eventual stale expiry restores ordinary animation. |
| Donor's two call sites are branches of one sender, whereas target `CL_SendMove` has separate private and public senders with different buffer sizes and ordering. | **Adopt.** Integrate both target paths after their existing movement/ACK writes; preserve private three-command redundancy and public ACK/movement order. Neither donor call site proves target coverage. |
| Full renderer palette/descriptor/BLAS lifetime is safe to port directly. | **Unreviewed.** Keep the target static-joint and animated-BLAS evidence above as an open architecture gate; do not claim avatar parity from codec integration. |

The first transport slice must cover a full private movement packet, ACK
backlog, public movement packet, and a pose too large to append. A live
desktop/VR session and visible-and-shadow pose agreement remain the end-to-end
proof after implementation.

## Local Astra transport review disposition

The focused Astra transport review requested changes while retaining the
existing adapter. It confirmed that unnegotiated desktop peers receive no VRIK
opcode, v2/v3 service size checks cover complete bodies, and no second
movement protocol is needed. Compilation is not a live cross-play result.

| Ranked finding | Disposition |
| --- | --- |
| **P1:** Delayed post-signon offers could allow a donor to send an unreliable pose before the reliable capability reply, causing a disconnect; a donor also clears capability on map change while the target server retained it. | **Addressed in `275b94c2` and `f7b6a603`, runtime pending:** include capacity-checked offers in serverinfo and renegotiate on every map. While version is unknown, discard the rest of an early pose datagram without admitting its body or dropping the peer. |
| **P1:** Target mid-session demo recording reconstructs signon without the selected VRIK version, so playback would reject later recorded poses. | **Addressed in `275b94c2`, runtime pending:** reconstruct the selected v2/v3 offer before any pose; demo playback latches the version without sending a network capability reply. |
| **P2:** Backward demo seek keeps future VRIK sequence/generation cursors and rejects earlier replayed poses. | **Addressed in `275b94c2`, runtime pending:** reset entity pose caches on backward seek while keeping the recorded protocol version. |
| **P2:** Disconnect or player-slot reuse can leave a previous occupant's tracked pose in the recipient cache until timeout. | **Addressed in `275b94c2` for a known predecessor, runtime pending:** clear on scoreboard departure and entity removal; retain the last stream generation so delayed predecessor datagrams cannot revive it. A predecessor with no previously received pose has no generation floor. |
| Replace the transport with a new reliability layer. | **Reject:** the reviewed packet framing and per-peer capability adapter remain the narrowest fit. |
| Server legacy pose mirror duplicates normalized pose state. | **Revisit after behavior is proven:** remove if no consumer requires it; keep raw v2 bytes only where exact v2 relay needs them. |

The follow-up Astra review requested three corrections before transport or
avatar parity can be claimed. Its static packet-budget check passed for both
public and private movement paths; the remaining issues are ordering and
presentation, not packet size.

| Follow-up finding | Disposition |
| --- | --- |
| **P1:** A separate reliable offer immediately after serverinfo may arrive while an inherited client is still precaching; its keepalive parser treats that as fatal. | **Addressed in `f7b6a603` and `ffc03c83`, runtime pending:** build ordinary serverinfo at full capacity, then append both offers in the same message only when they fit. Optional negotiation never reduces model/sound precaches or sends a separate loading-time message. Keep early-pose discard. |
| **P2:** On a slot departure, the client used to mark whichever unreliable generation was most recently cached as retired. If replacement B arrived ahead of departure A, this permanently rejected B. | **Provisional in `8a4f19f4` and `533ba400`, runtime pending:** a generation-specific retirement avoids clearing B, but the new reliable-buffer barrier can delay ordinary state and is being replaced by admission confined to VRIK. Do not treat the barrier as final transport parity. |
| **P2:** The sender mapped head/hands with tracking yaw while stereo presentation resolved view yaw separately in mouse-yaw mode; its public hand origin also included HMD translation omitted by the local viewmodel and muzzle. | **Addressed in `f445318c` and `ffc03c83`, runtime pending:** the view owner exposes resolved presentation yaw and hand origin to sender, stereo head offset, viewmodel, and crosshair muzzle; private body-owned roomscale movement keeps its gameplay mapping. Pending local yaw is included in the resolved presentation transform so a held smooth turn continues transmitting poses. |

The static review also identified the write-only server legacy pose mirror and
the always-false `keep_capability` reset argument as later deletion candidates.
No live mixed-peer or transform parity test has been run.

## Retirement architecture correction

A focused source review of `533ba400` identified two risks in the provisional
full-buffer retirement barrier. Its runtime did not expose a verifiable Astra
model/effort setting, so this is a source-backed review finding rather than a
completed Astra sign-off. Runtime behavior still needs the deferred mixed-peer
tests.

| Finding | Disposition |
| --- | --- |
| A separate retirement-only reliable send adds an acknowledgment wait before ordinary suffix updates. Continued routine writes can overflow the client buffer and cause a gameplay disconnect. | **Adopt:** remove the byte boundary and retire-only send. Optional VRIK admission should wait for ordinary reliable capacity, while ordinary state continues to drain. |
| `Send_Spawn_Info` can clear/rebuild a reliable buffer while the saved byte boundary still refers to old contents. | **Adopt:** delete byte-offset ownership rather than patching each buffer-replacement call site. |
| Clearing samples on an empty player name is enough to reject delayed old poses. | **Reject:** an old pose may arrive after a replacement name, and a player with no previously received pose gives the client no old generation to lock. |
| Reliable generation admission for a negotiated VRIK version can gate optional datagrams independently of gameplay. | **Adopt as the next transport design:** announce the exact slot/generation only when it fits after ordinary state; until then the client discards optional poses. Revoke admission on empty-name updates. Preserve v2/v3 wire-body compatibility without silently changing their semantics. |

## Focused Astra Vulkan avatar review disposition

The second local Astra review inspected the existing MD5 visible draw, ray-query
skinning, frame task graph, and donor IK. These are architecture constraints for
the avatar adapter, not evidence that an avatar currently renders.

| Review finding | Disposition |
| --- | --- |
| Visible alias drawing uses its own matrix and skin-selected model data, while the TLAS path negates pitch and can select skin-zero geometry. Palette sharing alone could produce a body/shadow mismatch. | **Adopt:** prepare one immutable per-entity render record for this submission: chosen model geometry, presentation transform, and optional palette. Use the visible entity as the transform reference and consume the same record from drawing and ray-query shadow building. Preserve the current TLAS owner. |
| `Shaders/md5.vert` and `Shaders/skinning.comp` read the same 12-float joint matrix layout; visible draw binds set 3, while animated BLAS consumes a device address. | **Adopt:** pack one live palette and expose it through both existing bindings, with identical pose offsets and zero blend for both consumers. Keep vkQuake shaders, multiview, weighted-skinning variants, and normal-animation fallback. Do not mutate shared model headers. |
| Draw and TLAS tasks have no dependency edge between them; donor rig state is mutable. | **Adopt:** solve and upload tracked poses once after entity collection and before both consumers; mirror this in the serial path. Include offscreen players whose shadows can remain visible. Key records by entity/model generation and frame submission, then freeze them until the frame-slot fence. |
| Donor CPU rig solving is reusable, but the target MD5 loader used to free joint metadata after building static GPU data. | **First visible adapter committed in `1b3cae61`, `c7c98dbb`, `7580b1c0`, and `54e9b3c3`:** `Mod_GetMD5Skeleton` retains model-owned joints and poses; the donor-derived CPU solver writes a frame-owned Vulkan palette. Ray-shadow use and visual parity remain pending. |
| Dynamic storage growth retires the old allocation, while `R_FlushDynamicBuffers` flushed only the current one. A later allocation could strand written palette bytes in noncoherent memory. | **Addressed in `a0b09f45`:** flush retired mapped dynamic allocations at the pre-submit point, after writer tasks finish, as required for host writes by the [Khronos Vulkan memory specification](https://docs.vulkan.org/spec/latest/chapters/memory.html). Review any transient descriptor ownership against [Khronos descriptor lifetime rules](https://docs.vulkan.org/spec/latest/chapters/descriptorsets.html). Pack palettes in one storage slice per submission, subject to storage alignment and `maxStorageBufferRange`. |

The first renderer proof is one compatible Ranger with inherited head/hand
solving, visible from desktop and both eyes with matching ray-query shadow,
including nonzero pitch and differing body yaw. Then verify tracking loss,
two simultaneous poses sharing one model, offscreen shadowing, allocation
growth, and frame-slot reuse. Canonical avatar presentation identity, donor
normal parity, multisurface coverage, and conservative IK culling bounds need
more evidence before claiming parity.

The scheduling seam is implemented after `R_MarkSurfaces` stores efrag entities
and after the frame-slot fence/buffer swap; visible entity draws and TLAS build
depend on palette preparation (`Quake/gl_rmain.c`). The serial path prepares
immediately after `R_MarkSurfaces`, before entity drawing. The shadow path traverses all dynamic
entities, including offscreen players, so preparation cannot be limited to
visible draw chains. A prepared record is immutable for the submission and
keyed by entity identity, model identity, and the current frame slot; a frame
slot may be reused only after its existing fence. This is an implemented task
edge, not a measured performance result.

The first proof also needs a real compatible player MD5 asset. This tree does
not ship a Ranger `.md5mesh`; vkQuake's replacement loading currently depends
on `r_enhancedmodels` (`Quake/gl_model.c`). The tracked-avatar path must obtain
the inherited rerelease Ranger through the existing model loader when the
user's game data provides it, and fall back to ordinary MDL animation when it
does not. Disabling enhanced models must not disable desktop play or gameplay.

For the Vulkan adapter, reserve a contiguous storage slice for all prepared
tracked-player palettes in a submission, then bind an exact-range descriptor
to that slice for visible MD5 draws. Ray-query skinning reads the matching
device address and joint offset. Descriptor sets and any retired storage
allocation remain alive until the frame-slot fence; a later storage-pool
growth must not change the descriptor already recorded in a draw command.
This reuses the existing dynamic storage allocator and the vkQuake shaders.
First gate multisurface and skin selection against the actual visible and
BLAS geometry, since skin-zero shadow selection can differ from a visible
player skin. A tracked player's culling bounds must include the solved pose;
static MD5 animation bounds alone can drop raised or extended hands. Retain
stereo-union culling and evaluate cheap per-joint influence bounds before
considering CPU vertex skinning or disabling culling. These are implementation
constraints, not completed behavior or measured speedups.

A candidate bound avoids CPU skinning every vertex each frame. The shader sums
per-influence `R_j * q + t_j * w`, where `q` is the loader's stored weighted
local position and `w` is its separately normalized quantized byte weight
(`MD5_BakeInfluences`, `Shaders/skinning.inc`). At load time, group vertices
and bound each group's **total** `q` and `w` contribution per joint (including
zero when a vertex does not use that joint). Each frame, transform those
per-joint intervals by the solved palette, add their AABBs by interval sum,
then union the group AABBs. That construction encloses the shader's weighted
sum in real arithmetic; add a conservative floating-point margin before using
it to reject a visible entity or shadow caster. A simple union of individual
joint boxes is insufficient. Measure bound tightness and CPU cost on large
maps before keeping it; if it is too loose, preserve conservative rendering.

## Desktop and VR cross-play gate

The shared engine must launch and play desktop vkQuake with no OpenXR runtime or
tracking. In one server, a desktop client and an OpenXR client must move, fire,
take damage, use weapons, and see the same entity/game state. Predictive
movement may be negotiated per client; VRIK capability is separate from it.
Only capable recipients receive tracked-pose opcodes. A desktop recipient in
this build may render a VR player's tracked avatar, but a legacy or
nonparticipating desktop recipient must keep ordinary player animation and
never see an unknown opcode. A VR player losing tracking must return to ordinary
animation without affecting either player's gameplay. Check both the public
server profile and the private profile with mixed clients; packet fixtures and
successful builds alone are insufficient for release acceptance.

| Mixed session | Required observable result |
| --- | --- |
| Target desktop plus target OpenXR on the public server profile | Both complete signon, movement, combat, and map transition; desktop sees tracked avatar only while valid pose data arrives. |
| Legacy desktop plus target OpenXR on the public server profile | Both share gameplay; legacy peer receives ordinary entity updates only, with no VRIK opcode. |
| Target desktop plus target OpenXR when the server permits a private predictive peer | Each client's selected movement dialect remains independent; the target desktop receiver may still opt in to VRIK, and neither packet path drops movement or ACKs to make room for poses. |
| Target desktop with OpenXR absent or unavailable | Desktop startup, menus, rendering, local play, and public network play work without creating an XR instance. |

The server capability/relay, client receive cache, and OpenXR-to-root-local
sender now build. Next is the shared visible/shadow MD5 avatar adapter. Judge
completion by the mixed session and rendered-avatar gates above: a client may
advertise receive capability while in desktop mode, but pose transmission
requires valid VR tracking.
