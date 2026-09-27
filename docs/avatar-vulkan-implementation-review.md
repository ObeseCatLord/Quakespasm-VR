# Vulkan avatar implementation: Astra senior review

Date: 2026-09-26. Branch: `2.0`. Review baseline: `c9b38b18`.
The inherited `master` renderer is the behavior reference. This review covers
the built-in avatar vertical path; the concurrent custom-package loader was
outside the reviewed diff.

The frame-owned render view remains the right boundary. It keeps the original
player entity as the render/BLAS owner, solves the canonical Ranger animation
and tracking before retargeting, and gives raster, culling, and ray shadows the
same selected geometry and palette. Missing assets fall back to the original
player model. Astra found no confirmed task-staging race or raster/TLAS
transform mismatch. Replacing vkQuake's renderer would add unnecessary state.

| Priority | Finding | Disposition |
| --- | --- | --- |
| P1 | Generic retargeting lacks inherited target endpoint, posture, and leg repairs (notably Dog and Fiend). | Partially addressed in `a45aa588`: desktop Dog/Fiend posture and arm repair and Vore outer-knee repair. Tracked endpoint and remaining physical-leg refinements are open. |
| P1 | Authored monster equipment still appears, and the player's equipped weapon is not attached. | Open: filter body indices and add frame-owned attached parts shared by raster, overlays, bounds, and BLAS. |
| P2 | Hip alignment omits the inherited static bind-floor correction. | Implemented in `a7e34d9d`: avatar-only retained bind geometry, source/target contact filtering, cached presentation offset. Asset-backed visual validation is open. |
| P2 | Presentation rotation left alias lighting in Ranger space while normals stayed in target space. | Fixed in `a1af91dc`: rotate the shade vector into target space. |
| P2 | Out-of-range Ranger run/stand fallback advanced in visible 100 ms steps. | Fixed in `c39168a1`: interpolate adjacent canonical poses without mutating entity lerp state. |
| P3 | Transparent alpha-sort distance and water classification still use original-model bounds. | Implemented in `610095a7`: use the prepared animated bound after palette preparation. |

Static rig resolution and repeated consumer validation are candidates for
centralization once the custom path settles. Preserve distinct main-thread
admission and frame-owned GPU publication; they have different lifetimes.

The smallest remaining implementation proof is two tracked peers and one
desktop peer using the same equipped alternate, with independent poses,
reference posture and floor contact, matching visible/shadow silhouettes,
and safe frustum-edge behavior. Also exercise avatar switching, tracking
loss, missing/malformed assets, and task rendering on/off. Synthetic parser
fixtures and compilation alone cannot establish that behavior; live headset
testing remains with the user.
