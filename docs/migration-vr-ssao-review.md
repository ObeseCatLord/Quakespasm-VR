# VR SSAO design review

The user wants vkQuake graphics effects to work in VR, with performance as a
priority and desktop SSAO unchanged. At the initial review, branch `2.0`
suppressed SSAO in OpenXR stereo; the implementation checkpoints below supersede
that historical limitation. Astra xhigh reviewed the existing vkQuake entity-occluder,
world-receiver GTAO pipeline against two-view Vulkan multiview and the
[XeGTAO authors' performance guidance](https://github.com/GameTechDev/XeGTAO/blob/master/README.md).

| Review recommendation | Disposition |
| --- | --- |
| Keep vkQuake's frame graph and SSAO algorithm as the VR correctness baseline, using per-eye 2D compute views into layered resources and one multiview composite. | **Adopt.** This preserves the defining world-only versus combined-depth ratio, MSAA receiver mask, and desktop behavior. |
| Run all compute stages twice with image barriers covering both layers on each eye. | **Reject.** Discarding layer 1 while processing eye 0, or layer 0 while processing eye 1, can destroy the other eye's data. Restrict discard transitions to the current layer or do one whole-image transition before both eyes. |
| Reconstruct AO with each eye's FOV tangents, not the symmetric center projection. | **Adopt.** Per-eye depth already includes relative eye rotation. Check horizontal and vertical pixel angular scale in the VR evaluator; the current radius assumes they match. |
| Keep full-resolution/default-on VR SSAO as the final performance policy. | **Defer.** First establish a reference, then compare VR-only half-resolution evaluation with depth-aware upsampling, and a full-resolution reduced-sample variant. Do not change desktop quality, precision selection, or defaults. |
| Share the Hilbert lookup table and vertex shader; avoid a wholesale algorithm replacement or async queue. | **Adopt.** A CACAO/ASSAO port is unjustified until the existing pipeline's total GPU cost is measured. Async scheduling cannot remove the depth dependencies. |
| Preserve debug, MSAA, stencil and OIT interactions. | **Adopt.** The composite must select each eye's stencil, depth, AO and prepared-depth layers. Entity-pass stencil clear and marking must both be restored. |

The first implementation target is a **VR-only full-resolution reference**, not
a speedup claim. Its per-eye 2D views reuse the current compute shaders. The
composite uses array views and `gl_ViewIndex` under one multiview draw. All
desktop SSAO code paths and cvar behavior must remain unchanged. Graphics
Options uses one quality setting for both rendering modes.

Implementation status: the full-resolution reference is present on `2.0`.
`r_ssao` selects off, low, medium or high in Graphics Options for both desktop
and OpenXR. Desktop runs its existing SSAO shader variants; OpenXR runs the
per-eye adapter and stereo composite at the selected quality. The desktop
default and algorithm remain unchanged.
A follow-up Astra xhigh review found no concrete regression in per-eye
barriers, image views, multiview composite, toggles, or teardown. Local engine
build and SPIR-V validation pass. A disposable simulated-Monado Linux run of
the shared-setting build completed the 24-probe OpenXR renderer matrix with
stereo SSAO enabled,
including MSAA, OIT, indirect rendering, resize and pause. A second run with
explicit fixed foveation confirmed the shading-rate attachment was active
through the same matrix. Focused desktop and OpenXR runs at default
`r_ssao 1` confirmed that compute uses the matching
one-layer or two-layer resource path. These runs did not use Vulkan validation
layers and do not establish binocular appearance or performance acceptance.

Next, compare total-frame and SSAO GPU time for AO-off, the
full-resolution reference, and a VR-only half-width/half-height candidate with
depth-aware upsampling. Evaluate p95/p99 frame time and missed-frame risk, not
only individual dispatch time. Keep the half-resolution path only if it gives a
repeatable total-frame gain without unacceptable thin-contact loss, halos,
head-motion shimmer, cross-eye leakage or unequal-eye edges. The existing
lowest `r_ssao` quality already uses four samples, so fewer samples alone may
trade too much stability for little gain.

The opt-in `r_ssao_vr_half 1` candidate is now implemented for measurement;
its default is `0`, which retains the existing full-resolution VR AO. It keeps
world-depth preparation and the combined-depth mip pass at native scene
resolution, then evaluates and filters AO at `floor(width/2)` by
`floor(height/2)` per eye using mip 1 as its depth input (matching Vulkan's
mip sizing). The existing multiview
composite reconstructs each full-resolution receiver from four AO samples,
weighted by position and world-depth agreement. A VR setting change recreates
the AO resource views together; desktop SSAO keeps its original dimensions,
algorithm and `r_ssao` quality options. No additional pass owner was added.

For a comparison, keep the same map, route, eye resolution, MSAA, `r_ssao`
quality, foveation and mirror setting, and compare `r_ssao_vr_half 0` with `1`
after warmup. Include `r_ssao 0` to establish the non-AO frame cost. Record
`scr_speeds 3` total GPU, SSAO compute GPU, CPU/wait times and frame misses;
repeat across a contact-heavy scene and `mj4m1`. The half-resolution option is
experimental until it wins total frame time without visible AO regressions.
The Linux debug Meson and release Makefile builds and Vulkan 1.1 shader
validation pass, but no GPU timing or headset
image comparison has been captured. The local isolated Monado service failed
during device discovery before the game could run, so this change has no new
runtime evidence yet.

Astra's implementation review accepted the existing pass/resource owner and
the explicit composite push-constant flag, with three P2 corrections before
commit:

| Finding | Disposition |
| --- | --- |
| An unrelated low-resolution depth sample could still darken a full-resolution receiver when all bilateral weights were tiny. | Fade visibility toward neutral as total depth confidence falls; no matching sample leaves the receiver unoccluded. |
| The debug composite fetched mip 1 past an odd-size image edge. | Clamp its coordinate to the mip view extent; the ordinary composite already clamps all four candidates. |
| Odd-size mip 1 covers `2 * floor(full/2)` full-resolution pixels, while the evaluation projection used the whole frustum span. | Scale the per-eye evaluation span by that ratio and retain the original frustum origin. |

These are localized corrections to the opt-in candidate. Astra's focused
follow-up verified all three in the final source and recommended an
experimental commit. Performance and headset appearance remain unmeasured;
the default stays full-resolution.

The existing `scr_speeds 3` whole-frame GPU timer now also shows an `ssao
compute gpu` interval when AO work was recorded. The interval brackets
`R_ComputeSSAO` in the donor frame graph and includes its depth transition,
per-eye mip/evaluate/filter dispatches and barriers. It does **not** include
world-depth preparation or the entity AO composite. The query pool and frame
fence remain owned by vkQuake's renderer; normal play adds no timestamp
commands. Query readback masks the selected queue's `timestampValidBits` and
omits an ambiguous wrap or unavailable result, following the
[Vulkan timestamp specification](https://docs.vulkan.org/spec/latest/chapters/queries.html)
and [query-result rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html).
The Linux build passed. No AO or whole-frame performance comparison has yet
been captured from this instrumentation.

Source/build review cannot certify this effect. The acceptance scenes need
entity-on-world contact, thin occluders, asymmetric eye frusta, MSAA, all OIT
variants, odd target sizes, and debug visualization. Desktop fixed-scene
captures should remain identical. Headset viewing and eye-specific performance
measurements are deferred to the user's testing phase.
