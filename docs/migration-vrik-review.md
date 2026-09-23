# VRIK transport and Vulkan avatar adapter: senior review brief

Status: verified design brief with a focused Astra send-boundary review, not a
full renderer sign-off, implementation, or cross-play result. The
release goal is a single vkQuake-based Windows/Linux/ARM desktop and OpenXR
engine where desktop and VR peers share gameplay and can see the VR peer's
tracked avatar when available. A peer without tracking keeps ordinary player
animation. This is a solo-maintainer port, not a new networking or rendering
platform.

## Checked evidence

| Claim | Status and source |
| --- | --- |
| `2.0` has no VRIK codec, avatar source, or VRIK opcodes. | **Verified:** `rg --files Quake` for `vrik/avatar` and `rg` in `Quake/protocol.h`, `cl_input.c`, `cl_parse.c`, `sv_user.c`, `sv_main.c` found none on `2.0`. |
| Inherited transport uses v2/v3 bodies and explicit per-peer capability. | **Verified:** donor `Quake/vrik_codec.[ch]`; `protocol.h` defines `clc_vrikpose=5`, `svc_vrikpose=87`; `cl_parse.c:CL_OfferVRIKProtocol`, `sv_user.c:SV_ReadVRIKPose`, `sv_main.c:SVFTE_AppendPendingVRIK`. Donor server advertises through comment stufftext, relays only to capable recipients, and records sequence/generation. |
| Target opcode numbers 5 and 87 are available in the current target protocol declarations. | **Verified:** `Quake/protocol.h` declares client opcodes 0-4 and `clcdp_ackframe=50`; no target `svc` declaration uses 87. This does not establish compatibility with arbitrary external protocol dialects. |
| A VR muzzle pose already reaches the authoritative server, but no remote tracked avatar is relayed. | **Verified:** `Quake/vr_input.c:VR_InputPreparePrivatePose`, `Quake/sv_phys.c:SV_BeginPrivateVRWeaponPose`, absence of VRIK target transport. The private QSVR profile is per client and server-default off (`Quake/sv_main.c:SV_SendServerinfo`). |
| Donor reusable CPU code exists beyond the wire codec. | **Verified:** donor `r_vrik.c` is rig/skinning math and cache logic (~940 lines); `player_avatar.c` is identity parsing (~374 lines). Its actual render/skin integration is in donor `r_alias.c` and `r_avatar.c`, while target `Quake/r_alias.c` already owns vkQuake MD5 GPU skinning and draw calls. |
| Target MD5 skinning currently reads static joint matrices from the model's descriptor. | **Verified:** `Quake/r_alias.c:GL_DrawAliasFrame` binds `paliashdr->joints_set` with two animation-frame offsets; `Shaders/md5.vert` reads set-3 `joint_mats[]`. A live per-entity pose needs a transient joint-matrix binding or another proven adapter; changing frame numbers alone cannot supply it. |
| Animated ray-shadow geometry has a separate pose consumer. | **Verified:** `Quake/r_brush.c:R_BuildTopLevelAccelerationStructure` calls `Quake/gl_mesh.c:R_UpdateAnimatedBLASes`, which recomputes per-entity skinned vertices for ray-query BLAS. A visible VRIK palette alone would leave shadow geometry stale. |
| OpenXR tracker enumeration already exists in the target runtime boundary. | **Verified:** target `Quake/vr_openxr.cpp` discovers HTCX/MNDX tracker devices and publishes them in `vrxr_frame_t`; mapping them to VRIK hip/feet and avatar state remains unimplemented. |
| Exact cost of adapting donor IK to the target MD5 palette and preserving GPU skinning is known. | **Unknown.** Requires targeted layout/task/animation inspection and a vertical proof; no proof from matching MD5 names alone. |
| An older/noncapable peer can safely receive the private VRIK opcode. | **Contradicted by protocol:** it must never receive that opcode; ordinary entity animation remains the fallback. |

## Architecture choice and open decisions

| Decision | Current lean and rejected alternative |
| --- | --- |
| Wire ownership | Reuse donor `vrik_codec.[ch]` and v2/v3 framing with exact validation, sequence/generation, stale/inactive and map-transition resets. Put a small adapter at the existing `cl_input`/`sv_user`/`sv_main`/`cl_parse` owners. Reject a second movement stream or embedding all poses in QSVR usercmds: VRIK must be independently optional for public desktop peers and existing servers. |
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
