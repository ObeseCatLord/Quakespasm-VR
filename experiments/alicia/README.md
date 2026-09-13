# Alicia: MD5 conversion and direct VRM drawing spikes

This is an opt-in experiment on `codex/alicia-avatar-spikes`, based on `e821a718`.
It accepts only the inspected Alicia VRM, not arbitrary VRMs. No model, skin,
palette, or commercial Quake data is included in this directory.

## Result and architecture

Both paths render Alicia inside Quake, including through a second client's
ordinary remote-player replacement path. The converted package also loads in a
normal build with the experimental renderer compiled out.

MD5 is not a fundamental Quake requirement. This fork uses its skeletal loader
to populate model/cache records, bind matrices, semantic bone indices, and
weights. The existing Ranger animation and tracking code ultimately produces a
palette of posed joint matrices. That palette is a useful renderer boundary:
the new draw path consumes it without serializing the VRM geometry as MD5.

For this proof, the MD5 package remains the **selection, skeleton, bounds, and
pose bridge**. Its decimated geometry is still skinned before the direct draw
hook, so this is intentionally not an optimized final architecture. When direct
drawing is enabled, the visible body vertices, indices, authored normals, UVs,
and PNGs come from the original VRM. The hook replaces the body draw rather than
overlaying two bodies. Optional Ranger equipment is shared between both paths.

A later standalone VRM model path could supply the skeleton/presentation and
bounds directly, then reuse the animation/IK code. That does not require
preserving the custom-MD5 package's surface/texture limits or rewriting the
whole engine. Separating the pose code's skeletal view from `md5liveinfo_t` and
its model-cache ownership would make this boundary explicit.

## MD5 conversion

`convert_md5.py` runs in Blender, using its decimation modifier and interpolated
vertex groups. The tested Blender version is 5.2.1. It retains all 143 source
nodes, remaps the 19 body semantics, bakes 32 Quake units/meter, and applies the
rigid axis transform `(-z, -x, y)`. Each vertex retains its four largest positive
weights, renormalized. MD5 joint-local positions are derived from the exported
global bind matrices.

The final generated package has:

| Item | Converted MD5 | Direct VRM draw |
|---|---:|---:|
| Draw vertices, including necessary splits | 13,856 | 21,623 |
| Triangles | 17,488 | 31,798 |
| Skeleton nodes | 143 | Same pose palette |
| Mesh text | 2,757,106 bytes | No intermediate mesh text |
| Body surface/material ranges | 1 | 20, using 12 materials |
| Base-color images | One 2048² atlas | Six original 512²–2048² images |
| Decoded base-color RGBA | 16 MiB | 33 MiB |

The source contains 21,529 vertices when shared attribute streams are counted
once. The direct draw uses compact per-primitive vertex sets, yielding 21,623
vertices; it does not multiply entire shared accessors by each material.

The atlas has padded tiles and separates opaque from blended uses of the same
image. Crucially, Alicia's body, clothing, and eye UVs extend above V=1 and rely
on repeat sampling. The converter bakes two vertical repeats into those tiles
and adjusts their UVs. A first naïve atlas produced missing clothing; this was
fixed after inspecting in-game screenshots. This is a concrete example of why
blind automatic atlasing is not simply packing six PNGs.

Compromises: decimation at ratio 0.55; substantially reduced texture resolution;
blended details approximated by alpha testing; no MToon/sphere effects,
expressions, or springs. The single atlas consumes the v1 package's entire
16 MiB decoded image budget, so it has no glow image.

## Direct rendering

`Quake/r_alicia_spike.c` uses the pinned MIT-licensed cgltf in `vendor/` to read
the original GLB. A file-length/FNV fingerprint guard runs before parsing and
accepts only the known sample; this is an experiment guard, not a general input
validator or network content identity. The known sample SHA-256 is
`237bb02efadf8c13a114af91dd8e860173081457dee87017e51011c448d05dc2`.

The loader maps each skin's joint-list indices to source nodes, reads inverse
binds, and creates weighted joint-local positions and normal vectors. It
verifies that the selected MD5 bridge has matching node names and global binds.
It loads the original embedded PNGs without atlas conversion or resizing, then
skins into a separate per-player frame cache and draws indexed client arrays.

The spike preserves the inspected source's material separation, repeat/linear
samplers, alpha blending, culling, and VRM0 MToon render queues/depth-write policy.
All four blended materials in this specific file author depth writes on. Parts
are ordered by queue and then approximate view depth. This is not a general
solution to intersecting transparent triangles. Sampling filters are restored
to the sample's authored settings at draw time, including after texture reload.

Lighting is selectable between simple Quake vertex lighting and unlit base
color. Authored normals are skinned, whereas the MD5 path recomputes normals
from its simplified triangles; their lighting consequently differs even under
the same scene settings. The renderer does not implement MToon shading,
outlines, sphere effects, morph expressions, gaze, or springbones. The tests
hide the separately attached Ranger weapon when comparing body materials.

See [cgltf's documentation](https://github.com/jkuhlmann/cgltf) for the parser
boundary and [glTF skinning](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html)
for the inverse-bind/joint-list convention.

## Retargeting findings

The follow-up [retargeting investigation](RETARGETING.md) quantifies the
stretching across actual Ranger frames, compares FPSloppa's procedural IK,
and proposes a shared humanoid pose pipeline for MD5 and VRM.

Numerically valid retargeting is not the same as a good pose. Alicia's T-pose
differs from Ranger's authored reference pose. The raw existing retargeter
leaves awkward raised arms and an awkward weapon socket. A sample-only switch
enables the existing `desktop_refine` hand-position solver; that gives a more
recognizable gun-holding pose in both render paths. It does not constitute a
complete humanoid animation adapter.

Idle, movement, and attack frames rendered, but hands remain open, proportions
and weapon alignment need tuning, and hair/clothing simply follow their parent
bones. The MD5 package can be installed in today's engine, but should be treated
as a rough test avatar, not a finished artistic port. A proper rest-pose
adaptation and hand/socket calibration are valuable next steps independently
of the choice of renderer.

## Build and generate

From the repository root, with a new output package directory:

```sh
blender -b --factory-startup --python experiments/alicia/convert_md5.py -- \
  /mnt/s/code/mainspring/vrm_samples/AliciaSolid_vrm-0.51.vrm \
  /home/s/.local/share/quakespasm-vr-spikes/alicia/base/player_models/alicia

nix develop --command make -C Quake -f Makefile.linux USE_ALICIA_SPIKE=1 -j8
```

The converter refuses to overwrite an existing package. Its output includes
the usual `avatar.cfg`, `model.md5mesh`, and `skin.tga`, plus an informational
`conversion.json` that the existing package loader ignores. It only accepts
the fingerprinted source file.

`USE_ALICIA_SPIKE` defaults to 0. The build configuration stamp includes the
toggle, so switching it rebuilds objects with compatible header layouts. No
experimental controls or cgltf code enter normal builds. This spike is wired
only into `Makefile.linux`; Windows integration is not included.

For this session the experimental executable was copied to
`/home/s/.local/share/quakespasm-vr-spikes/alicia/bin/quakespasm-alicia`, then the
ordinary checkout executable was rebuilt with `USE_ALICIA_SPIKE=0`. The isolated
base has a link to the user's existing game pak and a real directory containing
the generated avatar. Original game configurations and assets were not changed.

The original VRM metadata and linked usage terms still apply to the source and
derived local artifacts; no model assets are included with this code.

## Try the local comparison

The saved build and package are ready on this machine:

```sh
/home/s/code/Quakespasm-VR/experiments/alicia/run-preview.sh vrm
```

Use `md5` or `unlit` instead of `vrm` for the other initial draw modes. The
launcher uses the isolated profile, disables audio, and runs a desktop preview
on `start`. F5 toggles MD5/direct VRM, F6 toggles the cosmetic Ranger weapon.
`ALICIA_SPIKE_ROOT`, `ALICIA_VRM`, and `ALICIA_QUAKE_DATA` override the local paths.

Console controls:

| Control | Meaning |
|---|---|
| `r_alicia_direct 0 / 1 / 2` | Existing MD5 draw / direct VRM with Quake lighting / direct unlit |
| `r_alicia_pose 0 / 1 / 2` | Raw existing retarget / bind diagnostic / existing desktop hand refinement |
| `r_alicia_props 0 / 1` | Hide/show the separate cosmetic weapon |
| `r_alicia_preview 0 / 1 / 2 / 3` | Ordinary remote-only behavior / local chase preview / turned front / turned side |
| `alicia_spike_stats` | Direct-renderer counts and CPU timing averages |

Local preview only applies on desktop with chase mode and Alicia selected. It
rotates the rendered copy for inspection; it does not alter server orientation,
physics, or aiming. Turn it off when testing normal multiplayer. This is not
a first-person body implementation.

## Validation and evidence

Tested with an offscreen SDL/OpenGL context on the NVIDIA RTX 4090, using actual
Quake game data and rerelease Ranger animation. Offscreen mode used the hardware
NVIDIA renderer, not a software rasterizer. No headset was required or tested.

- Experimental build and normal build succeeded.
- Existing avatar-retarget and renderer-boundary fixtures passed with their
  ASan/UBSan configuration. This does not claim the whole engine ran under ASan.
- The package loaded through the unchanged MD5 package validator and renderer.
- A second client received the avatar descriptor and rendered the remote
  subject in both MD5 and direct modes, with local-preview mode disabled.
- A normal-build second client rendered the converted package without any
  experimental renderer code.
- A same-size copy of the VRM with one changed byte was rejected before parsing;
  the MD5 body remained visible and direct draw counters stayed zero.
- A real video-mode restart from 1280×960 to 1200×900 recreated the GL resources;
  the direct model and its owned textures still rendered correctly afterward.
- In-game screenshots cover the material comparison and movement/attack poses.
- `git diff --check` and launcher shell syntax checks passed.

Observed incremental CPU averages for the direct renderer were about
**0.51–0.52 ms skinning/lighting and 0.15–0.16 ms draw submission per visible
avatar/frame** in these desktop runs. High-resolution SDL performance counters
were used for these final measurements. They exclude the existing pose solve,
the still-performed MD5 bridge skinning, most world rendering, and GPU execution.
They are not GPU timings, full-frame benchmarks, stereo results, or a crowded
multiplayer scalability result. Some early capture configs requested the
nonexistent `cl_maxfps`; final preview uses the correct `host_maxfps` control.

Runtime artifacts and logs live under
`/home/s/.local/share/quakespasm-vr-spikes/alicia/results/`:

- `md5-refined.png`, `vrm-refined.png`: paused, same-pose body comparison.
- `vrm-unlit.png`: original base-color appearance without Quake lighting.
- `vrm-moving.png`, `vrm-attack.png`: real game animation samples.
- `remote-md5.png`, `remote-vrm.png`: actual second-client screenshots.
- `stock-md5.png`: normal-build viewer, with raw existing retargeting.
- Build, fixture, comparison, and multiplayer logs; `conversion.json`.

## What remains beyond this spike

The direct renderer supports the case that preserving original VRM materials is
practical. The next architectural step is a format-neutral skeleton/pose view
and a VRM-owned model/bounds/selection path, removing the MD5 bridge and its
duplicate skinning. Keep format-independent geometry/texture budgets, not the
single-surface package's atlas requirement. Then test a few different authored
rigs, tune rest-pose/hand adaptation, validate tracking in a headset, and profile
several independently animated avatars in stereo. Dynamic VBO uploads or GPU
skinning are available optimizations if measurement warrants them.

Production support would also need proper VRM 0/1 metadata/semantics, generalized
materials and alpha ordering, decoded-resource limits, importer validation,
cache lifetimes, actual VRM bounds, and a content identity covering the VRM and
its import profile. The current multiplayer identity still names the MD5 bridge;
the direct override is local and the exact source is pinned by this experiment.
