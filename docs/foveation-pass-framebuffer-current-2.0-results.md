# Optional density pass/framebuffer constructor results

2026-10-02. Existing F06, no production code changed. Luna/xhigh implements one
new native fixture and two conditional-spy guard lines in the existing fixture.
Main reviews the complete final delta, requires exact density-image/color-slot
correspondence, then independently compiles and runs both fixtures with GNU11,
SDL3, Wall/Wextra/Werror and section garbage collection. All four main compile/
run exits0; existing acquisition matrix remains unchanged with the guard unset.
[Before-code plan](foveation-pass-framebuffer-2.0-plan.md).

The new fixture includes existing render_acquire_fixture.c and actual r_passes.c
constructor/compiler/destructor owners. It preserves native frame descriptions
and result arrays; only prepared device/settings/image inputs and Vulkan calls
are controlled. No real device, driver, borrowed resource or submitted GPU work.
Native4xMSAA/stereo/AO1 settings retained; R_SSAOEnabled is the inherited cvar spy,
so this is not actual AO computation evidence. Density framebuffer attachment
identity is checked for every scene-slot/borrowed-map pair, not merely a recognized
image class. Source policy and ownership remain in the native renderer.

| Case | Actual native constructor observation |
| --- | --- |
| Valid density resources | Density passes across all three native variants, native stereo masks and sample/attachment metadata; six density framebuffers use the exact two color slots by three map views. Native cleanup retires all admitted pass/framebuffer handles once; repeat cleanup adds no nonnull destruction. |
| Second density pass rejected | One successful pass owned, second call returns negative memory error with deliberately nonnull unowned output. Native constructor returns false and makes no later ordinary calls. Failed output never published; native destruction retires only admitted handle and clears secondary-context render_pass fields. |
| Second density framebuffer rejected | One successful framebuffer, second call rejects with nonnull unowned output. Native six-slot partial array retains only first valid handle; later arrays remain absent. Native cleanup retires successful framebuffer, accepts null slots, then retires passes exactly once. |
| Subsequent ordinary construction | After each cleanup and explicit prepared-input reset to no density, actual native ordinary pass/framebuffer construction succeeds with stereo/MSAA4/AO1 settings. This proves subsequent constructor usability; it does not execute the GL resource-coordinator/runtime recovery path. |

The warp render pass has a separate owner; the fixture explicitly retires it
rather than claiming R_DestroyRenderPasses owns it. Borrowed image/view destruction
is prohibited. Owning render-pass/framebuffer arrays and secondary-context render_pass fields
are cleared. Cached physical binding render_pass values and the internal context
binding table are not claimed cleared. Healthy native reconstruction overwrites
bindings through the normal compiler. Astra requires reconstructed configured
contexts to point to live admitted passes; nonzero alone is insufficient.

Main evidence: FastGames/qsvr-density-constructor-0l6_mc49, strict argv/source hashes
in main-build-run-receipt.json, separate build/run logs and binaries. Density
fixture SHA256ce028dc7f5f5b9c6e4457c52b375a8658b5e69a6ccc27345e394d7b607ed3f70;
unchanged acquisition fixture SHA256eef5c814b0b8b0c95ca5aa1a7fa3f3583f47f32fb6b56049dcf9b853bb726624.
Worker evidence /tmp/FastGames-openxr-pass-framebuffer.mCmANF records separate
successful builds/runs and an initial overbroad later-allocation assertion failure
note. Its raw initial failed output was not retained; do not claim it was.
Main independently confirms correct early-return policy by reading the constructor.

Official interfaces support creation/error and cleanup expectations:
[vkCreateRenderPass2](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateRenderPass2.html),
[vkCreateFramebuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateFramebuffer.html),
[vkDestroyFramebuffer](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyFramebuffer.html).
Actual driver errors, allocation/provider readiness, image layouts and FB/META
hardware compatibility are not proved by fake dispatch. Existing prepared GL
OFF/recovery and loaded-scene abort/ordinary attachment results remain separately
accepted in [recovery evidence](foveation-recovery-current-qualification-2.0.md).
Protected foveation GPU output remains distinct.

Local Astra/xhigh accepts native constructor rejection/partial cleanup under
this scope after the [adopted corrections](stereo-resource-boundaries-2.0-review.md).
Luna strengthens configured-context assertions to require live ledger membership
and renames ordinary construction to identify cleanup/input reset. Main reviews
and independently rebuilds/runs the final fixture with the same strict flags:
compile0/run0. Evidence: FastGames/qsvr-stereo-review-final-q38i0meu,
main-receipt.json and density-build.log/density-run.log. Final binary SHA256
7a751036f7c9d79e3711d2a4da06befac0d9e0265cc580576a124510ad57ae79.
The unchanged acquisition matrix already passed independently with default guard
unset. No GPU submission or production change; no whole F06/F10 or goal closure.
