# F06 actual optional density-view constructor results

2026-10-01. No production edits.148-line reviewed main fixture reuses the existing
renderer fixture and actual gl_vidsdl.c GL_CreateXRImageViews/DestroyXRImageViews.
Only the inherited destroy spy is conditionally replaced with a tracked spy;
ordinary fixture behavior is unchanged. Controlled XR metadata and Vulkan
dispatch, no driver/GPU/runtime image creation.

Wall/Werror GNU11 SDL3 compile0, run0 with
OPENXR_IMAGE_VIEW_FAULT_PASSED. Five finite cases:

| Constructor input | Native observed ownership/result |
| --- | --- |
| Valid paired maps | Three color and three density views created; repeated creation makes no calls. Native final cleanup destroys density views before color views, each exactly once. |
| Second density-view call rejected | Error output is deliberately nonnull but not owned. Earlier successful density view retires; remaining density calls stop, all three color views survive, existing backend-failed latch set. |
| Later density image absent | Actual metadata validation rejects the optional set; first density view retires, all color views survive, latch set. |
| Later density extent insufficient | Native minimum extent from image dimensions/max texel rejects it; partial density view retires, all color views survive, latch set. |
| No borrowed density images | Three ordinary color views only, no backend-failure latch invented. |

All cases retain ordinary color views until final native cleanup. Repeated
constructor entry adds no metadata queries or views. Tracking accepts only
successfully created handles, rejects duplicate/unowned destruction and requires
all successful handles retired. Borrowed VkImages are not destroyed by the view
owner. Each retained color/density view is independently matched to its
corresponding borrowed image through the successful create-call record; a view
of a different recognized image cannot pass. Source has no new resource manager/
state machine or substituted policy. Both new components reject NDEBUG builds
because their acceptance checks require assertions.

Before-code scope and official reference are in
[the F06 selection/constructor plan](foveation-device-selection-final-2.0-plan.md).
The existing [OFF/recovery receipt](foveation-recovery-current-qualification-2.0.md)
separately establishes prepared latch/no-view OFF behavior and actual loaded
scene recovery. This slice invokes a real native constructor to establish the
missing view-failure premise; it does not execute all creation/recovery in one
physical FB/META session. Allocation failure, render-pass/framebuffer rejection,
protected GPU output, image layout/producer readiness and actual provider
qualification remain distinct F06 boundaries. No whole F06/F10 closure.

Private evidence /tmp/qsvr-final-qualification-thchgzi8:
foveation-device-current/image-view-fault;
logs/foveation-image-view-current-{build,run}.log, both exit0 before senior changes;
foveation-device-current/image_view_fault-reviewed;
logs/foveation-image_view_fault-reviewed-{build,run}.log, both exit0 after the
source-image correspondence assertion. Local Astra/xhigh review completed;
main verified effective routing, checked both findings and incorporated the
minimal fixture corrections. [Senior disposition](foveation-device-final-2.0-review-brief.md#final-disposition).
No assets, user settings, deployed server or main/reference changes.
The unchanged normal renderer fixture also compiles/runs0 after its six guard
lines, with OPENXR_ENABLE_PASSED; logs/foveation-enable-after-guards-{build,run}.log.
