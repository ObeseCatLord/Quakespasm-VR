# Alias GPU replay prerequisite

Status: plan and local Astra design disposition precede implementation.
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
