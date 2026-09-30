# Tracker source checkpoint

2026-09-30. Source inspection of FBT-001..005 and XR-008 at `923be876`.
No builds, fixtures, runtime probes or hardware trials were performed. This
checkpoint identifies implementation owners; it does not qualify a device.

## Reused reference and runtime boundary

The primary reference is `51b452c018273647dcf94f4628a370267ff8fa91` in the
read-only QuakeSpasm OpenVR checkout. Full source comparisons found no changes
in `vr_fbt.c`, `vr_fbt_filter.c`, `vr_fbt_profile.c` or `vr_fbt_storage.c`.
These inherited modules retain identity reconciliation, bounded filtering,
calibration capture/strict profile parsing and persistent storage. Meson and
the native Makefile include them. Their OpenVR-free interfaces remain reusable;
another tracker manager or profile format is unnecessary.

`vr_openxr.cpp:create_htcx_actions/refresh_htcx_trackers/locate_trackers`
adapts persistent runtime paths and uniquely resolved role actions. Enumeration
retries size changes, suppresses ambiguous identities/roles and invalidates old
availability before a failed refresh. Device slots are preserved while an
identity remains present; a full list can reclaim only an absent identity.
Persistent paths are converted to bounded, namespaced digest identities for the
existing serial field. New hardware without an attached supported role requires
a runtime role assignment or VR restart; actions are not rebuilt live.

`create_xdev_trackers` is the optional MNDX revision-3 preview adapter. Discovery
requires the exact vendored ABI revision and system/function support. Its ABI
has no tracker class or connection predicate, so only pose-capable devices whose
display name contains `tracker` are exposed. The session's list/spaces are
created at startup, not a general hot-plug enumeration service. Safe MNDX serials
are retained; other serials are digested. Extension absence/setup failure is
optional. These boundaries are explicit limitations, not evidence of broad
hardware compatibility.

## Identity, calibration and consumer paths

`vr_input.c:VR_InputFBTReconcile` consumes each completed OpenXR sample ID once,
using local monotonic observation time rather than confusing predicted XR time
with the filter's clock. Head/hand slots are excluded. Finite, valid and tracked
poses enter the inherited manager; velocity validity is independent. Safe
serials identify saved bindings; a serial-less connected slot receives a
session-only identity which is retired on disconnect. The manager retains
serial assignments across loss, refuses duplicate/ambiguous identities and
never infers a body role from enumeration order.

`VR_InputFBTReset` preserves explicit safe-serial bindings and clears sample,
ephemeral, filter, cached-target and calibration continuity. The archived
enable toggle and reference-space changes use this owner. Disabled trackers
do not prevent ordinary head/controller VR. `VR_InputCommands` reapplies the
preference after runtime teardown through `VRXR_SetTrackerEnabled`.

`VR_InputFBTCalibrateBegin/CaptureSnapshot/CalibrateAccept` use the retained,
verified Ranger reference, floor space, explicit role bindings and one neutral
capture per completed sample. A missing reference cancels capture. Preview
does not replace the active profile; acceptance uses existing save/select
publication, and cancellation keeps the prior profile. `menu.c` owns the FBT
page, role cycling, profile selection and capture/accept/cancel/save/reset
commands. `VR_InputFBTCalibrationVisualSnapshot` publishes detached targets to
the existing Vulkan debug pass with sample continuity checks. Inherited axis
colors/lengths remain; generic markers replace OpenVR render-model objects.

`VR_InputFBTBuildFilterInput/AppendTargets` apply saved anatomical corrections,
including angular point velocity, through the existing bounded filter. Identity,
floor/reference/model admission and finite wire bounds gate the result; stale
cached output expires. `vrik_codec.c` retains bounded optional lower-body masks
and version conversion. `r_vrik_render.c` samples admitted lower targets while
preparing the same frame-owned palette used by native draw, equipment and
shadow consumers. Unsupported lower-body input leaves normal upper-body/native
presentation available. This is not a new movement stream or a general FBT rig
solver.

## Remaining qualification

The final Linux/ARM pass must exercise actual backend-to-manager-to-pose
consumers: no trackers/extensions, duplicate/reordered identities, offline saved
serials, explicit reassignment, loss/recovery and stale output, reference changes,
calibration cancellation and save failure, profile reload, optional version
relay and independent remote poses. Check the real menu/debug draw consumers
and native desktop fallback. Source comparisons alone do not prove rendered
alignment, hot-plug behavior, network delivery or runtime-extension support.
Live device trials remain with the user. No new tracker implementation gap was
demonstrated by this bounded comparison.
