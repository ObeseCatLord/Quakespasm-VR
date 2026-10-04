# Rooftop interactable rendering audit

Date: 2026-10-04. Behavioral evidence references branch `2.0`, `7593b052`, before the fix. **scope_done:** QC, entity transport, and renderer investigation; native descriptor/entity-state probe; subsequently approved two-site graphics correction. Changes are limited to those sites in `Quake/r_brush.c` and this document. No rebuild, commit, installed-asset writes, or interaction with the user's running game. Concurrent calibration/configuration and AO/dynamic-light audits remain outside this ownership.

## Proven defect and causal explanation

**Indirect brush graphics draws double-apply the instance-buffer slot offset.** This is a concrete out-of-range shader read, not a speculative rendering toggle or an authored transparency effect.

Evidence:

- `Quake/r_brush.c:2931`: graphics descriptor binding 1 covers one slot: offset `slot * MAX_MODELS * sizeof(bmodel_instance_t)`, range `MAX_MODELS * sizeof(bmodel_instance_t)`.
- `Quake/r_brush.c:848`: graphics draws bind the descriptor for the latched slot.
- `Quake/r_brush.c:1237` and `:1281`: ordinary indirect draws and indirect showtris still push absolute base `slot * MAX_MODELS + 1`.
- `Shaders/world.vert:60`: the shader reads descriptor-relative index `base - 1 + submodel`. Its stereo variant uses the same expression.
- `Quake/r_brush.c:917`: CPU writes the transform at absolute index `slot * MAX_MODELS + submodel`.

With `MAX_MODELS=8192` and 64-byte records, the native GDB probe recorded:

| Slot | Descriptor offset | Descriptor range | Pushed base | Shader index for pillar `*34` |
| --- | ---: | ---: | ---: | ---: |
| 0 | 0 | 524288 | 1 | 34, valid |
| 1 | 524288 | 524288 | 8193 | 8226, invalid |

Slot 1 therefore requests physical byte `1048576 + 34*64`, beyond the entire two-slot allocation. `Quake/r_brush.c:4438` advances the slot each GPU-lightmap frame. Invalid transforms on alternate slots explain disappearing surfaces that look transparent/strobing. Exact invalid-read appearance depends on GPU robustness/driver behavior.

**Proven:** descriptor offsets/ranges, pushed bases, shader addressing, alternating slots, and actual replicated states. **Likely cause of the reported appearance:** this proven defect; it matches all three affected object families and the selected-only pillar condition. A visual before/after correction was not performed, so complete symptom remission remains a verification target.

## Actual map and packaged QC

The installed loose `maps/start.bsp` is the Rooftop hub (BSP2, 1,061 authored entities). It contains doors `*29–*31`, skill pillars `*34–*38`, and breakables `*68–*70`, `*75`. The start position is `-400 0 136`. `qrt_naitelveni` is a separate hub destination, not the map needed for this reproducer.

`pak0.pak` supplies `progs.dat`; no QC source files were found in the pack, and its appended ZIP directory is empty. Actual compiled statements were decoded privately, rather than assuming the adjacent AD source is identical:

- `func_skill_check`, statements **82755–82762**: compare `self.style` with `skill`; matching pillar gets `frame=0`, others `frame=1`.
- Statements **82763–82768** update particle emitters and schedule another check after 0.1 seconds. They do not pulse alpha, effects, or colormod.
- `func_skill_touch`, statements **82770–82877**, changes the selected skill and invokes that check.
- Door and breakable initialization was inspected in the packaged program; the native client states below establish their resulting rendering properties.

The native run at skill 1 found pillar entity 237 (`*34`) at frame 0; entities 242/247/252/257 (`*35–*38`) at frame 1. Doors 202/203/217 and boards 319/321/323/333 were frame 0. **Every listed entity had alpha byte 0, effects 0, and colormod `[32,32,32]` on both sampled slots.** Alpha 0 decodes to fully opaque (`Quake/protocol.h:258`), and color 32 is neutral.

`R_IndirectBrush`, `Quake/r_brush.c:878`, excludes nonzero frames. Consequently the selected pillar, doors, and boards enter the defective indirect path; unselected pillars use the direct path. This precisely explains the selection discriminator.

Transport remains coherent: `Quake/sv_main.c:2587` copies QC frame, `:2594` encodes alpha, `:2598` encodes tint, and `:2617` copies effects. Alpha's wire-minus-one encoding (`:1465`) is reversed at `Quake/cl_parse.c:1040`; tint is read at `:1115`, effects/alpha assigned at `:1278`/`:1285`, and frame at `:1319`. Brush opacity consumes decoded alpha at `Quake/r_world.c:1508`; there is no evidence of a tint/effect-induced alpha oscillation.

## Upstream and legacy comparison

Upstream reference `vkquake-upstream/master` = `749b4fd4`, and the clean adjacent vkQuake checkout `4bc898f2`, use **offset 0, whole-buffer range** (`../vkquake/Quake/r_brush.c:2553`) with the same absolute base (`:914`, `:1039`). That combination is valid. This branch retained its absolute base after introducing slot-scoped graphics descriptors.

Legacy OpenGL reference `../quakespasm-openvr`, `bd923e92`, draws brushes directly (`Quake/r_brush.c:138`, `:213`), without this descriptor/indexing mechanism. Its texture selector honors nonzero frame as alternate animation (`:64`), consistent with the packaged pillar QC. These are source comparisons, not new runtime visual comparisons.

## Minimal patch plan, proposed ownership, and verification

Proposed implementation ownership: **`Quake/r_brush.c` only**, the two graphics push-base sites. Set their low-bit base to **1**, because their bound descriptor already selects the slot. Preserve the high z-bias bit, slot-specific clustered-lighting descriptors, frame gating, shader ABI, and all adjacent systems.

Approved implementation plan: upstream's whole-buffer descriptor needs an absolute base; our half-buffer descriptor needs a relative base. Change only those two bases to `1`, retaining slot descriptors rather than replacing their layout. The shader's `base - 1 + submodel` then selects the same first record in either half: record 0 remains the reserved world/identity record, and submodel 1 remains record 1. Keep the separate high-bit z-bias composition unchanged. This preserves the existing renderer with two constant changes. Prepare the diff on branch `2.0`; defer the shared build and native captures until main coordinates them.

Do not change `R_IndirectComputeDispatch` at `Quake/r_brush.c:3964`: its compute descriptor covers the whole buffer (`:2367`), so its absolute base remains correct. Direct draws correctly push zero instance base (`Quake/r_world.c:1260`). No protocol, QC, shader, screen/menu, calibration, or configuration changes are required.

Verification target: fixed-camera hub captures spanning both slots must retain every affected surface; touch each pillar and confirm only its authored texture/particles change; open doors and break boards normally. Exercise desktop and stereo, tasks on/off, indirect showtris, map reload, and GPU/CPU lightmap transitions. Check graphics indices stay inside each descriptor and compute still addresses the intended absolute slot. Coordinate before rebuilding the shared diagnostic binary.

Private reproducibility artifacts: `/tmp/rooftop-interactable-audit/` contains setup, disassembler, GDB probe, launch script, decoded statements, and `native-state.json`. Successful probe used the supplied debug binary with private base/user directories and read-only asset links. Assets and raw diagnostic logs remain local. Startup required short argv paths (256-byte cmdline limit) and `-noextmusic` to avoid an unrelated diagnostic-library symbol mismatch.

Risks/limits: visual remission and VR qualification remain unverified; invalid GPU read behavior is implementation-dependent. No evidence supports replacing entity transport or disabling indirect rendering as production policy.

## Implementation ready for coordinated build

The approved correction is present at current `Quake/r_brush.c:1238` and `:1283`: both graphics bases are `1`, with explanatory comments. Reviewed diff contains exactly those two sites; compute's absolute base and z-bias composition are unchanged. `git diff --check` passed. Descriptor-address verification against the native receipt passed 32,764 combinations covering both slots, all nonzero submodel records, and z-bias on/off. These checks establish addressing correctness, not visual remission.

Ready for main to coordinate the shared native build. Reuse the private GDB setup after adjusting line breakpoints for the two inserted comments; require base `1` or `0x80000001` on both slots. Then obtain actual consecutive visual frames in desktop/stereo with tasks on/off, and capture a before/after pillar selection using the authored touch callback. Doors and boards must retain their surfaces on both slots and operate normally. Compilation and these native visual checks are pending; no shared build was started.

## Coordinated-build results and main disposition

The fixed assertion-enabled engine compiled successfully. Fourteen actual
Rooftop desktop captures with rendering tasks enabled span both instance slots:
selected/unselected pillars, closed/open doors and intact boards retain their
surfaces. Native authored pillar touch changes selection; doors open normally.
Main visually inspected the paired contact sheet. Observed graphics bases are
1 or 0x80000001, preserving the independent z-bias flag. Receipt:
`/tmp/qsvr-rooftop-frames-kyenhjrb/frames/receipt.json`; contact sheet:
`/tmp/rooftop-interactable-audit/desktop-contact.png`.

Rooftop stereo readback captured both actual eye images at slot 0 only. After a
debugger-injected Vulkan readback, a later frame stalled in Task_Join while
workers were idle. This private instrumentation outcome is unresolved and is
not relabeled as a passing Rooftop stereo-slot-1 test or proven production bug.
Board destruction, tasks-off and old-base visual comparisons were not completed.
All owned diagnostic processes were stopped.

Main separately accepts the normal presented-mirror evidence in
[the final graphics toggle audit](graphics-toggle-followup-audit.md): 156 captures
from the same fixed build exercise tasks-enabled desktop and both simulated eyes,
both graphics slots, and native AO/light menu changes; repeated and cross-slot
images are identical. That evidence is from AD start, not a substitute claim
that every Rooftop interaction was performed in stereo.

Disposition: retain the two-site descriptor-relative correction. The source
proof, actual affected-object desktop remission and independent ordinary stereo
both-slot results support delivery without replacing the native renderer or
adding a new readback path. The diagnostic stall and exact test limits remain
recorded. Physical headset behavior remains the user's validation.
