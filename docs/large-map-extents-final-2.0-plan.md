# F07 native large-map surface-extents qualification

2026-10-01. Before code. Frozen F07 worker/serial and repeated-map boundary;
performance measurements remain user-deferred. Native gl_model.c loads textures
and surface extents through its existing indexed task owner, then joins before
publication. Mod_CalcSurfaceExtentsTask delegates the same CalcSurfaceExtents
used for the serial worker-entry path. Do not add a loader/renderer or change
product task policy to make qualification easier.

Minimal native fixture: one test translation unit includes entire gl_model.c
and adds one post-load helper, <=100 lines. Replace only the original gl_model
object in the existing assertion-enabled Meson client link; retain normal main,
all other current native objects, Vulkan, rendering tasks and assets. This is a
test binary, not a production entry point. Existing compile/link arguments are
reused, with exact emitted arguments retained privately.

The helper runs on the main thread after draw_done is joined (GL_EndXRFrame).
Snapshot each loaded world's texture minima/extents, overwrite only those four
values with sentinels, then invoke actual native Mod_CalcSurfaceExtentsTask
serially and require exact equality for every surface. Retire temporary memory
before returning. No copied extent arithmetic, fabricated success or texture
reload/resource replacement. Separate debugger observation confirms the initial
map load actually entered the extent task on a native worker. This establishes
the surface-extents subset, not full texture/alias/loader equivalence.

Use private licensed id1 plus installed Mjolnir resources; native command queue
loads mj4m1 twice with normal teardown/reload. Require signon4, settled loaded
frames, worker observation in each load, exact serial equality, rendered native
screenshot, clean validation and natural exit. Preserve failed attempts. Asset
directories are read-only symlinks under a disposable writable profile, never a
symlink of the entire game directory. No user configs/saves/server changes.

Actual loose-path scan and all68 installed PAK directories contain no mfxsp17
entry. Record that absence explicitly; another map cannot certify that named
case. Other formats/texture/quad batches/stereo/effects remain separate F07/F05
boundaries. Main owns this slice; the Luna QC worker writes disjoint files.
No new senior review is needed for this reuse-only routine qualification unless
it reveals a production defect or estimate/ownership incompatibility.
