# VR SSAO design review

The user wants vkQuake graphics effects to work in VR, with performance as a
priority and desktop SSAO unchanged. Branch `2.0` currently suppresses SSAO in
OpenXR stereo. Astra xhigh reviewed the existing vkQuake entity-occluder,
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
desktop SSAO code paths and cvar behavior must remain unchanged. A separate
VR policy may leave the effect opt-in until performance has been measured.

Implementation status: the full-resolution reference is present on `2.0`.
`vr_ssao 1` enables it in OpenXR; `r_ssao` still selects the quality level,
and desktop SSAO retains its existing default and shader variants. VR SSAO
defaults off until its frame-time cost and binocular output can be assessed.
A follow-up Astra xhigh review found no concrete regression in per-eye
barriers, image views, multiview composite, toggles, or teardown. Local engine
build and SPIR-V validation pass; these checks do not establish visual or
performance acceptance.

Next, instrument total-frame and SSAO GPU time and compare AO-off, the
full-resolution reference, and a VR-only half-width/half-height candidate with
depth-aware upsampling. Evaluate p95/p99 frame time and missed-frame risk, not
only individual dispatch time. Keep the half-resolution path only if it gives a
repeatable total-frame gain without unacceptable thin-contact loss, halos,
head-motion shimmer, cross-eye leakage or unequal-eye edges. The existing
lowest `r_ssao` quality already uses four samples, so fewer samples alone may
trade too much stability for little gain.

Source/build review cannot certify this effect. The acceptance scenes need
entity-on-world contact, thin occluders, asymmetric eye frusta, MSAA, all OIT
variants, odd target sizes, and debug visualization. Desktop fixed-scene
captures should remain identical. Headset viewing and eye-specific performance
measurements are deferred to the user's testing phase.
