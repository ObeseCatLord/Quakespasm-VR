# Alias GPU replay prerequisite

Status: historical prerequisite; unused replay/retention has been removed under
the focused [Vulkan scope disposition](openxr-device-reconstruction-2.0-plan.md#focused-astra-max-scope-disposition).
General live device reconstruction is deferred. The plan below records the
original design/review and is superseded by the removal plan at the end.
Parent: [device reconstruction](openxr-device-reconstruction-2.0-plan.md).

Behavior: after GPU-only retirement, the same loaded alias/header/surface/model
identities can recreate Vulkan mesh resources without reading altered source
files or cancelling admitted custom avatars. Initial drawing and full model
disposal retain vkQuake behavior. This prerequisite does not yet switch devices.

Verified reference: primary51b452c0 gl_vidsdl.c:886 separates GPU retirement
from model re-upload, but its OpenGL retained mesh arrays are not present here.
Donor/current GLMesh_UploadBuffers owns layout conversion, buffer usage, heap
allocation, staging, descriptors and addresses. MDL/MD3/MD5 upload inputs are
freed by their loaders; existing alias GPU deletion also frees CPU skin texels.
Current custom avatars are immutable admitted snapshots. Reparse is therefore
not a drop-in adapter preserving header identities.

Choose final upload retention at existing aliashdr_t surfaces and one shared
Vulkan upload helper. Retain transformed vertex/index and skeleton-index bytes
with checked sizes. Do not retain original files or duplicate a second model
registry. Reuse retained MD5 poses where possible; any fallback owned joint
copy must be explicit and freed with its owner. Keep additional alias RAM
visible as an unresolved qualification cost, and avoid unconditional copies
of already retained skeletal poses across surfaces if a safe shared lifetime
exists. Brush/BSP arrays are outside this slice and will be regenerated.

The MD5 loader creates its retained skeleton after surface uploads. Keep a
temporary joint-source borrow marked not replay-ready while that loader is
active, then bind the already retained, byte-identical pose span before freeing
the temporary joints. If no matching retained span exists, materialize one
owned copy at that narrow completion hook. This avoids a permanent joint copy
per surface and a large transient duplicate during upload. Preflight refuses
an unfinished borrow; full failed-load disposal never dereferences it.

Write ownership: gl_mesh.c, gl_model.h and glquake.h. A narrow gl_model.c
lifetime hook may be needed solely to bind existing retained MD5 poses before
temporary loader arrays are freed, or dispose replay inputs on a demonstrated
loader failure; report/reopen before a loader rewrite. Expected300–500 changed
lines; stop if another asset owner/state machine or broad loader rewrite is
needed. Dedicated servers should not acquire replay payloads.

Implementation:

1. Surface-owned immutable replay bytes/size metadata, preserving exact MDL/MD3
   final layout and MD5 four/eight-weight layouts. Initial and replay uploads
   must use the same buffer allocation/usage/staging/descriptor code; addresses
   are recomputed from the current device, never retained as replay inputs.
2. GPU-only retirement preserves skin texels, replay data, frames and tracked
   geometry qualification. Existing full deletion wraps that retirement and
   disposes all CPU-owned replay bytes and skins exactly once. Retire private
   prop BLAS before its input buffers. Do not destroy entity BLAS implicitly
   through unrelated model data: complete transaction will retire those owners.
3. Replay all loaded mod_known alias variants and private prop chains through
   existing ownership traversal. Preflight that every uploaded surface has
   valid replay data before destroying it; reset/partial failed loads are safe.
   Do not reload models via Mod_ForName or replace existing aliashdr_t pointers.
4. Do not activate live device reconstruction until all parent resource owners,
   garbage drains, typed qualification and partial unwinds are integrated.

End-of-full-implementation acceptance: both Linux builds; real initial and
replayed MDL, multi-surface MD3, MD5 four/eight weights, generated held geometry
and admitted avatar/props; exact upload bytes/layout, live current-device
addresses/descriptors, preserved skins/identities and full-disposal safety.
Repeated same-device retirement/replay is an owner proof only; complete loaded
scene/new-binding/connection proof belongs to the parent. No builds/tests now;
final local Astra source review follows integration. User live/performance and
Windows qualification remain deferred. Native Linux ARM is engine end-goal work.

## Local Astra implementation disposition (2026-09-29)

Lovelace verified the four-file implementation against the plan and actual
primary/vkQuake sources. The existing allocation/staging/descriptor/address
blocks are reused unchanged by initial upload and replay. The524-line diff
contains326 lines relocating163 unchanged allocation lines; this small estimate
overrun does not add an asset owner, renderer or reconstruction state machine.

| Finding/recommendation | Disposition |
| --- | --- |
| Private prop upload passes a stack-local identity pose; the MD5 completion hook visits only the main surface chain. The retained prop borrow would dangle and all-model preflight refuses replay. | Adopted: copy only the private prop's joint bytes during upload while live; mark owned/replay-ready. Full disposal frees that copy. Main MD5 surfaces still borrow their byte-identical retained skeleton. |
| Keep GPU-only retirement and full CPU disposal separate. | Implemented at the existing mesh deletion owner; skin texels, frames and immutable payload survive retirement. Private prop BLAS retires before inputs. |
| All loaded alias variants and private props need preflight before mutation. | Implemented through existing mod_known ownership traversal. No source reparsing or header replacement. |
| Caller joins tasks, retires entity BLAS, establishes GPU completion and drains deferred garbage on the old heaps/layouts. | Retained as parent transaction requirements; new APIs are not wired into live device switching. |

Final local Astra Max source review accepted the private-prop correction,
including caller lifetime, readiness, GPU-only retirement and full disposal.
Linux/ARM software checks and RAM/runtime qualification are deferred until the
full implementation pass. No builds/tests or device reconstruction trials have
run for this slice.

## Transient-upload restoration plan (before implementation)

Objective: eliminate normal-load RAM/copy overhead introduced solely for unused
live-device replay. Verified current consumers: GLMesh_UploadBuffers owns initial
layout conversion, staging copies and Vulkan allocations; its private creation
helper is called only there and from the unused replay traversal. No production
caller invokes the replay entries. R_StagingUploadBuffer copies all bytes before
returning; GPU completion is not needed to free the original CPU upload source.
Primary's existing-context mode toggle and vkQuake's same-device restart do not
need these additional model mirrors.

Use the existing creation helper with explicit borrowed byte spans/sizes,
without another payload owner. Free transformed vbodata immediately after its
staging upload; use loader-owned indices, skeleton indices and joints directly
while valid. Remove replay-only copies, gpu_upload field/type, MD5 completion
hook and check/retire/replay traversal/API together. Collapse the CPU-disposal
switch back to the existing full deletion entry; preserve GPU handle clearing,
native heap/descriptors/garbage, private-prop BLAS retirement and skin disposal.
Preserve model-owned MD5 skeletons, avatar bind/prop data, culling metadata,
model identities and all four/eight-weight/MDL/MD3 layout math.

Write set: Quake/gl_mesh.c, gl_model.c, gl_model.h, glquake.h and this document.
Expected roughly200–260 source lines, predominantly deletion; no renderer,
loader, staging, avatar or protocol replacement. Local Astra scope review
recommends this bounded deletion. Final source review follows; builds/tests
remain deferred until full implementation is finished. End qualification checks
native models, avatars/props, map disposal and desktop/stereo rendering; no
device-replay test is a requirement for the deferred transaction.

Restoration source integrated: thirteen added and194 removed source/header lines.
Local Astra Max personally reviewed the complete change, native staging copy
implementation and MDL/MD3/MD5/private-prop callers; no P1/P2 findings. Removed
replay identifiers have no remaining source references. GPU allocations, upload
bytes/layouts, descriptor/address handling, skin disposal and actual avatar data
remain. Transformed upload vertices are freed after synchronous staging copies;
indices and joints are borrowed only during their valid loader call. Source
review and git diff --check only; builds/runtime/ARM qualification stay deferred.
