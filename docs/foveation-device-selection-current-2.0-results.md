# F06 native device-family selection results

2026-10-01.441-line reviewed fixture includes existing renderer fixture and entire native
gl_vidsdl.c GL_InitDevice. No production edit, copied selector or assigned
candidate result. Main verified effective Luna gpt-6-luna/xhigh context, read all
output and made the integration corrections below. Estimate was explicitly
reopened from360 to450 for required discovery replies and independent cases.

Wall/Werror GNU11 SDL3 compile0; actual native run0 with17 case messages and
OPENXR_DEVICE_SELECTION_PASSED. Six reusable guards leave the original renderer
fixture unchanged; its existing runtime preparation/recovery/attach cases also
compile/run0 with OPENXR_ENABLE_PASSED. No driver is asked to create a device.

| Input / actual create request | Observed selection |
| --- | --- |
| Startup XR, complete eye/offset/flags/GPU | FDM and offset extensions/features, no KHR |
| Ordinary desktop, both candidates | KHR, no FDM/offset |
| Ordinary desktop, FDM only | FDM prepared, no offset/KHR |
| Explicit fixed startup XR, FB available without eyes | FDM, no offset/KHR; selection does not activate a frame profile |
| Requested sample shading or paired render-size override | KHR |
| Offset scene formats or required native MSAA format rejected | KHR |
| Individually missing eye, image flags, offset extension or offset feature | KHR |
| Individually missing non-subsampled support, RG8 format or two-layer support | KHR |
| Neither extension candidate, or -novr | No foveation feature family |

Each request checks selected extension/enabled feature and exact selected node
presence in a bounded pNext walk. Both feature families cannot be selected, and
unselected queried nodes cannot survive into the request. The reviewed version
also verifies FDM non-subsampled support is enabled, FDM dynamic and KHR pipeline/
primitive shading are disabled, and the desktop render-pass2 dependency is
present when required. Discovery deliberately advertises those unwanted feature
bits as supported so their removal is observable. These are software
discovery/selection cases; native GPU KHR submission and FB/META profile/runtime
setter evidence remain separate existing receipts. No actual FB provider pass.

Main corrected initially weak negatives before any passing run: seven cases
now copy the successful FB inputs and change only one capability, rather than
already lacking eye prerequisites. Existing native GL_InitDevice consumes those
query inputs to derive selection. Expected family is checked independently at
the actual final VRXR_CreateVulkanDevice/vkCreateDevice request. Native earlier
allocations are freed before the creation intercept; no post-create queue mutex
or Vulkan resource is fabricated. Local longjmp stops before the driver, not
out of runtime teardown or a queue callback.

The first native compile links unsuccessfully because reused source references
core com_argc/com_argv. Main provides prepared empty CLI globals; device override
is absent, and native selection remains unchanged. Initial failed build log is
retained. No production defect established. The passing binary uses the same
unmodified native selection/source owners.

Controlled input supplies Vulkan discovery, support/profile flags and prepared
cvars; this is not an actual GPU/runtime feature probe or device creation.
QCOM offset extension case is covered here; EXT-offset API/dependency variation,
device-create driver error and real producer/layout/readiness remain distinct.
Default eye availability/opt-out/fixed behavior is in existing frame-policy
receipts; this does not certify active gaze or select implicit fixed mode.
Protected GPU output, allocation/render-pass/framebuffer faults and actual
FB/META/provider qualification remain separate F06 boundaries. Whole F06/F10
closure is unsupported. Windows and user hardware/performance tests deferred.

Private evidence root /tmp/qsvr-final-qualification-thchgzi8:
foveation-device-current/device-selection;
logs/foveation-device-selection-current-build.log (initial failed link),
logs/foveation-device-selection-link-fix-build.log (exit0),
logs/foveation-device-selection-current-run.log (exit0, before senior corrections),
foveation-device-current/device_selection-reviewed;
logs/foveation-device_selection-reviewed-{build,run}.log (both exit0, final17cases).
[Before-code plan/official references](foveation-device-selection-final-2.0-plan.md),
[actual view failures](foveation-image-view-current-2.0-results.md),
[verified senior brief](foveation-device-final-2.0-review-brief.md).
Local Astra/xhigh senior review completed; main verified the effective model/
effort, checked the source evidence and implemented both assertion improvements.
The affected final components compile/run0 after those changes. See the
[disposition table](foveation-device-final-2.0-review-brief.md#final-disposition).
Main/reference/user migration document, deployed server, assets and user
preferences untouched. No production defect or renderer rewrite was established.
