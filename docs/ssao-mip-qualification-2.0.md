# SSAO mip qualification against the merged shader reference

## Scope and reference

The original test-only acceptance extends `tests/ssao_shared_mip_vulkan_fixture.c`
and adds `tests/run_ssao_mip_vulkan.py`, directly atop upstream merge
`59c4df5c1fc0268c51ed7d56b34bc7f6e67bcbcf`. Subsequent qualification of the
final production shader root demonstrated a subgroup partial-tile defect.
The authorized shader fix changes only `Shaders/ssao_mip.inc`; it leaves the
renderer, previous rectangular mip implementation, and shared build graph alone.

The integration worker's existing seven-binding fixture correction was reviewed
before further edits. Its binding layout, `r32ui` tag format, dimensions, clear
dependency, and explicit sample scale are the baseline. The acceptance patch
retains the existing image/buffer/pipeline builders and scalar tile/filter
oracle. It extends their existing parameters to two layer views and two
descriptor sets; it adds no parallel graphics or compute implementation.

The renderer reference uses one sampled depth binding and six storage images:
five `rg16f` depth mip views and the packed mismatch image at binding 6.
Mismatch bits cover 128x128 regions; 8x4 bits are packed per 1024x512 texel.
The fixture models the renderer's separate 2D views into two image layers,
per-eye descriptors, and per-eye tag clear immediately before dispatch.

## Acceptance matrix

Each of the shared FP32, shared FP16, subgroup FP32, and subgroup FP16 wrappers
uses the same original reduction oracle. The subgroup device eligibility and
full/varying subgroup pipeline flags follow `gl_vidsdl.c` and `gl_rmisc.c`.
FP16 requires the renderer's `shaderFloat16` feature and extension.
Unsupported variants exit 77 and remain explicitly unqualified.

Eight extents (`32x32`, `33x19`, `17x17`, `6x16`, `128x128`, `129x129`,
`257x131`, `1025x513`) run at sample scales 1 and 2. Scale 2 reads an odd
full-resolution depth image, using `source_pixel = output_pixel * 2`.
Inset view rectangles in the odd/partial cases exercise the viewport guard.
Each extent/scale executes both eyes in two rounds, swapping matching and
mismatching inputs. Sparse disagreements cover 127/128 pixel edges, trailing
partial tiles, and 1023/1024 and 511/512 packed-word boundaries. The largest
case also checks literal masks `0x80000001`, `0x200`, and the next word's bit 0.

Every dispatch reads back all five mips and all tag words from both eyes.
The active eye is checked against the oracle, with exact mismatch flags and
missing-depth sentinels. FP32 permits one half-float ULP for contraction;
FP16 uses explicit half rounding in the existing arithmetic oracle and permits
four half ULPs. The inactive eye must match its previous GPU snapshot exactly,
including the initial poisoned tags. Independent clears must also remove the
previous round's mismatches without altering the other eye.

## Runner and qualification

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/run_ssao_mip_vulkan.py \
  --shader-root /path/to/final/production/Shaders \
  --output-dir /tmp/qsvr-anisotropy-integration-20261004/build/ssao-qualification
```

The output directory must be new and private to this checkout. The runner
requires committed shader/renderer inputs and a completed merge. It records
the reference SHA and hashes the real shader include closure before compiling
and after qualification. Each process has a bounded timeout. It uses `glslc`
for all eight production wrappers and `spirv-val` when available, then GPU
readbacks for the four ordinary wrappers. Compiler, validation, and GPU logs
plus `qualification.json` remain in the private output directory.

MSAA wrappers receive compilation/validation in this compute fixture. Actual
multisampled depth production is qualified separately through the native
renderer smoke; its results and the necessary post-fix repeat are recorded
below. No graphics pipeline was added to manufacture MSAA inputs. Local GPU
acceptance supplies no physical Frame/mobile or performance claim.

## Confirmed discrepancy and narrow shader fix

The complete four-variant production qualification used shaders identical to
`78819be538b0e8b7728718d0a4d178d62d6a5455`. The actual runner reference was
`cd83b749402fea2b0418594f1e458a81a3c689ca`, which imported the acceptance
fixture without changing shaders. All eight wrappers compiled and passed
SPIR-V validation. Both shared variants passed 16/16 cases. Both subgroup
variants completed the full matrix and failed only `6x16`, at scales 1 and 2.
Logs and exact source hashes are in `build/ssao-mip-final-788/`.

The scale-2 world-channel discrepancy is exact, not a tolerance issue. For
`6x16`, mip 1 is `3x8` and mip 2 is `1x4`. At the first mip-3 output, the valid
mip-2 column contains `8.25, 8`, while the next register column (outside the
mip-2 image) contains `8, 8`. `read2` clamps that neighbor to the valid column:
`(8.25 + 8.25 + 8 + 8) / 4 = 8.125`, or half bits `0x4810`. The subgroup
shuffle instead reads Morton lanes 0, 4, 8, 12 without that image-size clamp:
`(8.25 + 8 + 8 + 8) / 4 = 8.0625`, or `0x4808`. Both GPU precisions returned
exactly `0x4808`. The CPU-only derivation in `boundary-proof.log` confirms the
coordinate difference with the original scalar filter. Scale 1 produced GPU
`0x480a` versus oracle `0x4815` (FP32) / `0x4816` (FP16).

Shader fix `cf4cbbf299cd355d7da9ba13c66e25752891ed61` names the existing
mip-1 partial-tile predicate and reuses the existing shared reduction for
those workgroups, including its `read2` and `read3` bounds. Every invocation
in a workgroup takes the same branch, so all edge lanes reach its barriers.
Complete tiles retain the original optimized subgroup reduction. The shared
shader wrappers retain their original reduction unconditionally. Most lines
in the textual diff are indentation around the two existing blocks; no second
reduction implementation or renderer policy was introduced.

The fixture/oracle, cases, and one-/four-ULP allowances were not changed by
this fix. Its source was confirmed identical to production immediately before
the fix, including production at unrelated Bonk guard commit `1f2c44ec`.

## Post-fix qualification

Ran once after the shader implementation was complete and committed:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/run_ssao_mip_vulkan.py \
  --shader-root /tmp/qsvr-anisotropy-integration-20261004/Shaders \
  --output-dir /tmp/qsvr-anisotropy-integration-20261004/build/ssao-mip-partial-fix
```

Reference: `cf4cbbf299cd355d7da9ba13c66e25752891ed61`. On the local NVIDIA
GeForce RTX 4090 (subgroup size 32), shared FP32, shared FP16, subgroup FP32,
and subgroup FP16 each passed all 16 cases / 64 dispatches: 256 dispatches
total. This includes both eyes, independent clears, exact tag bits, odd/partial
extents, both sampling scales, and the previously failing `6x16` outputs.
Clang fixture compilation and all eight `glslc` compilations / `spirv-val`
checks passed. The runner returned 0 and confirmed unchanged source hashes.
Logs and `qualification.json` are in `build/ssao-mip-partial-fix/`.

## Separate renderer/MSAA qualification

Main reported native desktop renderer smoke passing all 12 combinations of
AO quality 1/2/3, half resolution 0/1, and MSAA sample count 1/4 in
`/tmp/qsvr-final-render-742mtlr6/desktop`. Main also reported real-GPU Monado
stereo renderer smoke passing those same 12 combinations in
`/tmp/qsvr-final-render-4_ydmka1/stereo/native.log`. These are renderer runtime
results, distinct from the compute fixture's MSAA compile/validation coverage.

Those initial renderer runs preceded the shader edge fix. After importing it
as `e2b0f1c6` and rebuilding the affected modules, the main integrator ran
`tests/upstream_ssao_native_smoke.gdb` again against the actual final renderer:
desktop and private simulated Monado stereo each passed all 12 combinations.
The final private receipt root is `/tmp/qsvr-final-render-kwvroiaa`. The desktop
captures show the stock start map. The default disabled VR mirror does not
produce window screenshots; stereo software qualification uses actual native
renderer assertions plus the independent two-eye GPU readbacks above.
Physical headset appearance remains unverified. Neither initial launch issue
required an engine or global runtime/GPU change: a private Monado stdin setup
and overlong fixture arguments were corrected using a pipe and relative paths.

Main separately reported the final production anisotropy acceptance passing:
actual texture-manager uploads and 2/4/8/16 GPU readbacks, with evidence under
`tests/.aniso-upstream-bonk-final-20261004`. That result is independent of the
SSAO shader fix and introduces no additional change or test run here.

The original acceptance commit is `093fe609db645308a19f5442b1c0b451a5c8c47d`;
the shader fix is a separate one-file commit identified above. The anisotropy
commit `046f0048276533828f5adb7213176c51f192360b` remains independently
preserved on `2.0`. No production-main checkout was changed by this work.
