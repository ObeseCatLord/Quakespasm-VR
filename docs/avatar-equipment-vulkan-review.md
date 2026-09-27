# Vulkan avatar equipment: Astra senior review

Branch `2.0`, reviewed 2026-09-26. Source behavior is `quakespasm-openvr/Quake/r_alias.c` equipment filtering and attachment. The selected-avatar render view remains owned by the original player entity; no renderer rewrite is warranted.

The verified rerelease pack in the local game directory matches `COM_VerifyRereleaseModelPack`'s Ranger mesh and animation CRCs. Its Gun and Axe are leaf joints. The inherited fully-owned triangle rule selects 170 Gun and 142 Axe triangles, and every selected vertex is unit-weighted to its sole respective joint. Therefore their geometry can be stored once in bone-local space and transformed rigidly per player. This conclusion applies to the byte-verified Ranger asset, not arbitrary MD5 replacements.

| Review recommendation | Disposition |
| --- | --- |
| Publish body and equipped prop as one complete frame decision. | Adopt. A missing prop or invalid attachment must fall back to the complete Ranger presentation before any raster or ray consumer sees the selection. |
| Use two source-owned static Gun/Axe meshes and shared static BLASes. | Adopt. Keep the existing animated body BLAS and original player entity. This removes per-frame prop skinning and BLAS refits. |
| Filter native equipment with immutable per-surface body index streams. | Adopt. Apply the inherited weight thresholds at private avatar admission; use the same selected indices for raster, overlays, and body BLAS. Omit empty filtered surfaces consistently. |
| Include attachment in overlays, bounds, and ray instances. | Adopt. Extend the frame record, make overlay masks cover body plus prop before outlines, and emit a prop TLAS instance even for offscreen players. |
| Merge source and target skeletons or add a heterogeneous animated BLAS. | Reject. The verified rigid weapon subset removes the need for joint concatenation and a second animated BLAS. |

For a canonical weapon vertex `x` stored in its bone-local space, let `G` be the solved Ranger Gun/Axe joint, `A` the inherited target-hand attachment, and `E` the player entity transform. Raster and ray instances must agree on `E × A × G × x`. Do not apply the target body's presentation scale to weapon geometry: the inherited socket scales the hand position while keeping the detached weapon's size and orientation.

Build private body index views and both canonical prop meshes at admission. Retain source UV/material data and recompute normals for the selected prop triangles. Use vkQuake's model upload machinery and existing deferred GPU destruction. Static BLAS input buffers need acceleration-structure build-input usage, which normal alias vertex uploads currently omit. The frame record references model-owned resources and publishes only after all required pieces are valid. Desktop socket readiness remains an optional refinement; a failed optional grip uses ordinary hand attachment rather than a weaponless avatar.

The first end-to-end proof is Knight beside a lit wall: intact hand with its native sword removed, canonical Gun/Axe switching, intermediate animation, both dominant hands, combined co-op overlay, frustum-edge visibility, and offscreen weapon shadow. Force missing prop resources and require full Ranger fallback. Repeat the essential selection and failure checks for one custom avatar and an empty filtered surface. Synthetic retarget fixtures and a successful build do not prove visual parity or GPU timing; live headset validation remains with the user.

## Static shadow lifecycle follow-up

Astra's implementation review (2026-09-26) confirmed that each private Gun/Axe root mesh should own one immutable BLAS for its whole surface chain. Reuse vkQuake's mesh heap, shared AS scratch, and deferred BLAS garbage. Do not create one BLAS per player or a second AS registry. Read each MD5 prop vertex directly with `sizeof(md5vert_t)` stride (88 bytes), float3 position, and 16-bit indices; the animated body's packed 12-byte vertex helper is unsuitable.

Source admission uploads the static prop buffers. The existing AS task must build pending source-owned BLASes before animated-body updates and TLAS emission, with transfer-to-AS-read and AS-build-to-AS-read barriers. Since the source model is not necessarily a server-precached entity, discover pending private roots in `mod_known`. With ray shadows enabled, keep the requested selection pending and publish complete Ranger until selected raster and ray inputs are ready on a later synchronized frame. The TLAS then adds one prop instance per eligible player with `entity * attached_prop_to_canonical`, including offscreen tracked players. Count and emission predicates must match. Reset must clear AS handles and device addresses idempotently before retiring mesh buffers.

Staging and render command buffers use the same Vulkan queue. A first regression proof uses two Knights with different poses sharing the same Gun/Axe BLAS, one offscreen caster, switching, and reset/reload. Raster attachment is committed; the optional desktop waist grip remains open.

## Integrated shadow-path review, 2026-09-27

The static prop BLAS builder and per-player TLAS instance are now integrated on `2.0`. Astra reviewed the code read-only after integration. The model-owned BLAS, existing AS task, and complete Ranger fallback during warmup remain the chosen design. The review found one shared-scratch synchronization gap at the existing brush BLAS build boundary; the brush build's final barrier now covers later AS scratch writes and compute accesses, in addition to reads. The prop builder also bars subsequent prop/body builds and TLAS reads. `R_RecordFrame` submits the AS context before scene work, and the next frame's begin task depends on the prior end task when task recording is enabled.

| Review recommendation | Disposition |
| --- | --- |
| Shared model-owned BLAS, with one TLAS transform per equipped player. | Adopted. No per-player rigid BLAS or parallel AS registry. |
| Build through the existing AS task. | Adopted. Scratch reuse and the brush-build completion barrier were tightened. |
| Complete Ranger fallback until the selected prop BLAS is ready. | Adopted. The staged selection is retried on a later frame. |
| Treat `avatar_prop_blas_built` as GPU completion. | Rejected. It means build *recorded*; the next frame sees it only after the prior frame submission dependency. GPU execution stays ordered on the same queue. |
| Check readiness, then request the address separately. | Adapted. The checked address accessor alone is sufficient. |

Local `build-nocurl` compilation passes. GPU validation and raster/shadow visual agreement are still unverified; a successful build does not close the equipment parity gate. Cold-start, RT off/on, offscreen players, unload/reload, and the earlier Knight scene remain the relevant checks. The user will handle live headset acceptance.
