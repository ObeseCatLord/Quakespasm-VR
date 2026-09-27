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
