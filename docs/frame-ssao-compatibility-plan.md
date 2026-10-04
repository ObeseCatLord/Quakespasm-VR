# Frame SSAO compatibility

## Evidence and boundary

- Verified in Frame's prior native launch log: Turnip Adreno 750, FP16 and ray
  queries are selected, but SSAO is disabled for missing subgroup operations.
- Verified: `R_SetupRenderPasses` rejects SSAO when `screen_effects_sops` is false.
  That flag describes the optimized shuffle/full-subgroup path, not baseline
  compute support. The AO mip pass is the only SSAO shader using subgroups.
- Verified: `ssao_mip.inc` already owns the shared depth filter, Morton tile
  mapping, edge replication and shared arrays for small subgroup sizes. Existing
  prepare/evaluate/filter/composite, MSAA, stereo and half-resolution paths are
  reusable. Extended storage image format support remains necessary.
- Unknown: which individual optimized-subgroup requirement Turnip lacks. No
  driver capability or GPU reset will be forced to get around the requirement.

## Minimal implementation

Keep the existing fast mip shader unchanged on currently supported devices.
Add a compile-time shared-workgroup reduction variant at the same mip owner:
64 local invocations produce the same 8x8 mip-1 tile, write existing shared
arrays, synchronize, then 16/4/1 invocations write mip 2/3/4. Preserve both depth
channels, sentinel handling, filter arithmetic and edge replication. Barriers
are reached by the whole workgroup; only uniformly empty mip-1 tiles return.
Reuse the four existing FP32/FP16, single-sample/MSAA wrapper patterns and
existing descriptor/dispatch/resource owners. Select fallback with no subgroup
pipeline flags only when optimized subgroup support is absent. Expose AO based
on actual remaining format requirements, rather than the optimization flag.

Rejected alternatives: replacing AO with another algorithm adds unnecessary
state/resources and changes desktop graphics; relaxing full-subgroup checks
without evidence risks invalid lane access. Shared reduction is a small adapter
and preserves the ordinary desktop fast path.

## Verification at implementation completion

Compile and validate every shader variant. Compare GPU-produced mip channels
against the existing filter/reference for full and odd/partial tiles, missing
depth, FP16 and MSAA if available. Build Linux and native ARM, then verify Frame
shader/pipeline creation and AO menu availability without resetting the GPU or
overwriting user configs. Device performance and appearance are not assumed.

Official synchronization reference:
https://docs.vulkan.org/spec/latest/chapters/shaders.html#shaders-scope-workgroup

## Astra disposition

The read-only review ran with `gpt-6-astra` and effective `xhigh`, verified from
its own local turn context. The reviewer did not have access to that metadata.

| Finding | Disposition |
| --- | --- |
| Use local invocation indexing and explicit 64/16/4/1 producers | Adopted: row-major 16/4 producer coordinates, retaining Morton mapping only for input tile ownership. |
| Shared partial-tile inputs must not be overwritten while readers use them | Adopted: an additional whole-workgroup barrier retires common edge-replication reads before fallback producers rewrite the shared array. |
| Preparation must precede uniform empty-mip return | Adopted: trailing mip0 preparation and original full-size dispatch remain unchanged. |
| Compilation must remove subgroup instructions and pipeline flags | Adopted: four shared wrappers exclude subgroup extension/builtins, optimized and fallback modules have mutually exclusive capability selection. |
| Degenerate extents need an explicit edge oracle | Adapted: shared path clamps at each valid preceding level, matching existing `read2/read3` small-subgroup semantics; existing desktop fast shader behavior is retained. |
| Keep intermediate precision and validate arithmetic/resources, not counters only | Adopted: existing filter and half2 intermediates remain; shader validation and actual compute readback target the fallback. Headset appearance remains user validation. |

The follow-up static SSAO review passes. Publication findings were adopted in
the existing R2 owner: enforce native architecture, exact inventory equality,
source-archive-bound wrapper/notices, and hash-verified immutable revision
objects before uploading. Manifest publication remains last, after public-byte
verification. No stable/main-channel object is written by the 2.0 publisher.

## Completed compute check

The FP32 single-sample shared shader was executed on the RTX 4090 against a
CPU reference for both RG16F channels at all five mip levels. The 32x32,
33x19, 17x17 and 6x16 cases passed, including missing-depth sentinels, trailing
full-resolution pixels and partial tiles. Finite results allow one half-float
ULP for division/contraction; sentinels must match exactly. Scene inputs use
power-of-two reciprocal depths to avoid ambiguous reference rounding.

`tests/ssao_shared_mip_vulkan_fixture.c` documents the compile/run commands.
All eight optimized/shared FP32/FP16 and single-sample/MSAA wrappers compile
and pass SPIR-V validation. Shared variants contain no subgroup instructions.
This does not establish headset appearance, FP16/MSAA readback equivalence or
performance, and does not resolve the earlier intermittent gameplay crashes.
