# Visible native stereo alpha inputs

2026-10-02. Existing F05 qualification, no production renderer change. The GPU
is available again; use the owned isolated simulated null Monado service and
native Vulkan/X11 mirror. Retain the earlier fault and all failed attempts.

Initial generated uniform indexed skins produced actual black transparent
strips, not absent geometry: B/E differ in7470pixels in one eye. Native
Mod_FloodFillSkin uses the top-left color as background and consumes the entire
uniform skin. Preserve that upstream behavior. The corrected test emitter gives
its skin one top-left255 guard (native explicit skip),63 fullbright color texels,
and UV1..6 interior coordinates. Geometry/308-byte format/parser paths unchanged.
Main independently checks packed header/skin/UV inputs and manifest hashes.
Luna/xhigh implemented only the emitter correction; main reviewed it.

Actual native late precache/Fitz static parsing create2 identities using normal
command scheduling. Both remain native aliases in wet/empty world leaves,
alpha128(.5) or ENTALPHA_ZERO1 on entity/baseline/netstate. Native efrags/PVS/
culling/sort/material/draw owners are unchanged. Native skin top-left255,
nonzero fullbright texture, gl_fullbrights1, alpha-list counts>=1/1 when enabled,
MSAA4/SSAO1/OIT0 and eye wet masks1/2 pass. Time is frozen normally by pause.
All16 B/E/W/C captures across2eye mirrors and2rigid roll arrangements complete
with normal GDB/engine exit0 and ALPHA_LAYER_CAPTURE_NATIVE_PASSED.
Main inspects E/C output: red wet and blue dry strips visibly render, including
through the actual water surface. This accepts the visible-input premise only.
Pixel composition acceptance is separately reviewed; do not infer it from lists.

Private receipts: FastGames/qsvr-alpha-output-atskpzr0 preserves initial black
strips and two distinct loader/runtime-path failures. Current colored root
FastGames/qsvr-alpha-colored-_ef3cp5j retains assets.json, generated/progs inputs,
alpha-layers.gdb, entry/log/exit, output/layers.json and16native PNGs. Observed
formats:64=A2B10G10R10_UNORM scene,43=RGBA8SRGB XR,44=BGRA8UNORM desktop.
GL_CreateMirrorResources uses a UNORM snapshot and raw bit-compatible image
copy; later UNORM blit preserves encoded numeric color rather than decoding it.
The official [Vulkan copy/blit specification](https://docs.vulkan.org/spec/latest/chapters/copies.html)
distinguishes those copy and conversion operations.

The test host under FastGames/qsvr-alpha-host-9x2fw7sl compiles the actual
cl_parse owner through tests/stereo_alpha_native_fixture.c, replaces only that
object in the existing assert-enabled debugoptimized native graph, and adds2
private fixture commands. Compile/link/narrow SDK relink each0. Current SDK4.8.1
headers match prior receipts; system SDL3.4.16 is observed. Executable-side
verified OpenXR loader discovery is reused. current-input-artifact-receipt.json
hashes245link/source/artifact records. Renderer source timestamps precede reused
objects as a sanity check; this is not a historical source-content proof.
It is a native component test host, not the shipping packaged executable.
Shipping0bd4ddb1 remains unchanged; its desktop/stereo/sixdof/liquid passes are
recorded separately. No production rebuild is needed for these test-only edits.

No physical tracking/gaze/provider/performance qualification, no validation layer,
no driver reset/reload, user input or unrelated runtime changes. Either-eye-only
bounds and other F05 owners remain open. No whole-goal/group acceptance.

The exact published tests/openxr-stereo-alpha.gdb recipe repeats all16 captures
in fresh output/userdir FastGames/qsvr-alpha-published-tjinncb7: actual formats,
identities/inputs/time/quality/leaf assertions pass; engine/GDB exit0 and main
independently verifies16entries/files plus recipe hash. A read-only bounded
kernel-log observation reports no new NVIDIA fault events. The owned isolated
Monado service is then normally stopped; unrelated runtimes/apps are untouched.

Local Astra/xhigh independently reproduces the8diagnostic order/material rows,
but identifies single-column upscaled witness coverage, unproven current-frame
presentation association and weak missing-layer alternatives. These are oracle
gaps, not demonstrated renderer failures. [Senior dispositions and next proof](stereo-alpha-composition-2.0-review.md)
retain composition as inconclusive and reuse native renderer/snapshot ownership.
Observed0..2byte order residuals are not used to fit a universal threshold.
