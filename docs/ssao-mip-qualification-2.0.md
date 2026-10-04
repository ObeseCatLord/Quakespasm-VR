# SSAO mip qualification against the merged shader reference

## Scope and reference

Test-only acceptance extends `tests/ssao_shared_mip_vulkan_fixture.c` and
adds `tests/run_ssao_mip_vulkan.py`. The shader and renderer reference is the
stable merge in `/tmp/qsvr-upstream-integration-20261004`; no renderer, shader,
previous mip implementation, or shared build graph is edited here.

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
  --shader-root /tmp/qsvr-upstream-integration-20261004/Shaders \
  --output-dir /tmp/qsvr-anisotropy-integration-20261004/build/ssao-qualification
```

The output directory must be new and private to this checkout. The runner
requires committed shader/renderer inputs and a completed merge. It records
the reference SHA and hashes the real shader include closure before compiling
and after qualification. Each process has a bounded timeout. It uses `glslc`
for all eight production wrappers and `spirv-val` when available, then GPU
readbacks for the four ordinary wrappers. Compiler, validation, and GPU logs
plus `qualification.json` remain in the private output directory.

MSAA wrappers receive compilation/validation only. Actual multisampled depth
production requires the existing renderer smoke in final integration; this
bounded fixture intentionally adds no graphics pipeline to manufacture it.
MSAA runtime remains a separate, explicitly unverified check. No physical
Frame/mobile or performance claim is made by local GPU acceptance.

## Reviewable result and pending final qualification

The additional test changes are based directly on merge
`59c4df5c1fc0268c51ed7d56b34bc7f6e67bcbcf`, including its existing fixture
compatibility fix. They live on the private `ssao-mip-qualification-20261004`
branch. The anisotropy commit `046f0048276533828f5adb7213176c51f192360b`
remains separately preserved on `2.0`; it is not folded into this acceptance
commit or its parent history.

An initial qualification against the stable `59c4df5c` shader root compiled
the fixture with Clang `-Wall -Werror`, compiled all eight wrappers with
`glslc`, and passed `spirv-val` for all eight. On the local NVIDIA GeForce
RTX 4090, both shared FP32 and shared FP16 passed all 16 cases / 64 dispatches,
including exact packed tags and both-eye isolation.

The subgroup FP32 run passed the first six extent/scale cases, then failed
`6x16`, scale 1, eye 0, round 1, mip 3, pixel 0, world channel:
GPU `0x480a` versus oracle `0x4815` (11 half ULPs). This exceeds the existing
one-ULP FP32 allowance. The observed run stopped there, so subgroup FP16 and
the rest of that subgroup matrix were not yet qualified. The shader's
optimized `gl_SubgroupSize >= 16` mip-3 reduction consumes shuffled mip-2
values without `read2`'s image-size clamp; the shared path uses that clamp.
That is a candidate explanation for this narrow partial-tile discrepancy,
not a production fix made by this patch. The oracle and tolerance are retained.

The finished runner/fixture now collect numerical failures while completing
the bounded matrix, emit a nonzero qualification status, and preserve the
first discrepancy per case in private logs. This failure-reporting adjustment
has not received another GPU run. At the user's direction, further GPU
acceptance is deferred to the main consolidated checks against the final
production shader root after Bonk integration. Supply that root explicitly;
the runner records its actual final SHA and rejects changing or dirty inputs.

Initial logs and source hashes are in the private ignored directory
`build/ssao-mip-qualification-59c4df5c/`. The remaining checks are the final
four-variant GPU matrix and the existing renderer's MSAA smoke. The subgroup
discrepancy needs disposition by the production shader owner; these test-only
changes deliberately neither suppress that case nor alter shader code.
