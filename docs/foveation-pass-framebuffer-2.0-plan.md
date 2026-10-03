# Optional density pass/framebuffer qualification

2026-10-02. Existing F06 only. Required remaining constructor premises:
render-pass/framebuffer Vulkan rejection and partial resource cleanup, retaining
ordinary full-quality stereo. Hardware FB/META/provider qualification remains
user-deferred. No new resource owner/state machine/render graph or production
rewrite. Reuse actual r_passes.c constructors and existing render_acquire fixture.

Minimal adapter: one conditional guard around the inherited vkCreateRenderPass
spy; default acquisition fixture unchanged. New fixture includes that existing
fixture with renamed main and custom creation spy, so actual native frame-layout,
pass compiler, creation and destruction functions remain the behavior owner.
Control VkCreateRenderPass2/VkCreateRenderPass/VkCreateFramebuffer/destroy dispatch
and native capability/settings inputs only. Native descriptions and handle arrays
must not be assigned as results. Successful fixture handles are unique and owned;
failed creation returns negative memory error with deliberately nonnull output,
which must never be published or destroyed. Accept null framebuffer destruction
as Vulkan permits, reject duplicate/unowned nonnull destruction.

Bounded cases: valid density constructors/ordinary cleanup; second density pass
creation rejected after one valid owned pass; second density framebuffer rejected
after one valid framebuffer. Calls stop at rejection; actual native cleanup retires
all successful handles exactly once, clears native handle arrays and the
secondary-context render_pass fields. Then
ordinary no-density pass/framebuffer creation succeeds with4xMSAA/SSAO1/stereo
retained; no runtime feature-family switch or implicit fixed foveation is tested.
Actual constructor recovery premise is distinct from the already qualified
GL resource-coordinator OFF/abort/ordinary attachment path. Borrowed images/views
are controlled but never destroyed here. Existing frame-acquisition test retains
its original desktop/stereo/SSAO/MSAA/OIT behavior.

Luna/xhigh owns only tests/render_acquire_fixture.c conditional spy guard and
new tests/openxr_pass_framebuffer_fault_fixture.c, no production/GPU/driver
changes or commits. Main owns source/reference analysis, culling probe, docs,
independent build/run and integration; no overlapping edits. Fixed contract;
missing evidence reports rather than broadening into adjacent systems.

Official constructor/destructor interfaces:
- https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateRenderPass2.html
- https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateFramebuffer.html
- https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyFramebuffer.html

No real Vulkan objects, submitted commands, driver error injection or benchmark.
This closes specific native ownership premises only after actual component runs;
protected GPU output and other frozen owners remain separate.
