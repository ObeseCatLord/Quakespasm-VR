# F06 profile creation and full-quality recovery

2026-10-01; production aa828629. Focused local Astra/xhigh review, main integration
and Luna/xhigh profile-fixture implementation. Main verified effective routing:
two Luna contexts and three Astra contexts, without agent resumption/rerouting.
The existing vkQuake renderer/device and OpenXR session owner remain.

## Behavior repaired

Before this change, optional density-resource failure bypassed the runtime OFF
setter, and failed OFF restoration only restarted application render resources.
The runtime could retain its previous coarse profile. Preparation now requests
OFF even when density views are absent or the optional backend failed. Confirmed
OFF preserves the session. Failed restoration releases VR-owned input before
abort/detach, retires borrowed views through the native callback, and permits
one ordinary attachment only after healthy teardown and compatible binding.
EXITING/session/instance loss and failed teardown preserve explicit-recovery
semantics. Density requests remain disabled on this device after that failure;
device capability/settings stay intact. No switch from FDM to KHR is attempted.
Fixed remains explicit-only, and unavailable eyes never select it.

## Actual source and native GPU evidence

| Boundary | Result and exact scope |
| --- | --- |
| Backend profile creation | Strict C++14/SDL3 compile and run0. Actual production create/update/destroy owners, controlled dispatch: off/fixed/dynamic eye chains; dynamic rejection retries static eye-tracked profile; fixed rejection preserves eyes; both eye failures leave mode2 off; mandatory off failure stops; negative error outputs are not owned; positive loss-pending retains valid child handle for ordered destruction and suppresses retry; partial off initialization stops and preserves successful handles for cleanup. |
| Renderer preparation/recovery | Actual gl_vidsdl.c fixture compile/run0. Valid paired aligned offsets; explicit fixed, opt-out, unavailable eye and menu; invalid offset requests off; failed restoration latches and clears frame activity; latched/no-view state still calls off, including negative rejection. Fresh input release is required at abort entry; terminal abort consumes retirement before detach exactly once. Healthy teardown admits one ordinary attempt, failed attachment leaves desktop dimensions; stop/loss/failed detach/ineligible binding cannot automatically attach. Resources and runtime calls remain spies. |
| Native loaded-scene recovery | DEBUG preliminary host full build0. Actual Monado simulated HMD and RTX4090 Vulkan with isolated validation1.4.357.20baseline frames of live e1m1, one controlled backend setter return-1, actual release/abort/detach and next-frame native reattach,20recovered frames with complete two-eye GPU submissions. Exactly2total native attachment entries and1retirement before final quit; native4xMSAA/SSAO1 and896x1007per eye retained. Full quality before/after, JSON/marker and GDB/process0 normal quit; no Vulkan validation/VUID/synchronization errors. Main inspected recovered native eye mirror: intact world/HUD. |

The GPU fault overrides only one backend return at its debugger boundary. This
runtime/device has no FB/META provider or density-map capability. It proves the
real loaded-scene retirement/re-attachment vertical path, not an actual FB setter
failure, borrowed density-image producer behavior, gaze or physical device loss.
The renderer fixture prepares the resource-failure latch/no-view boundary; it
does not inject errors into every real view/pass/framebuffer Vulkan constructor.
Existing actual KHR fixed/menu/unavailable/off evidence remains separately
recorded in [the F06 transition receipt](foveation-current-qualification-2.0.md).
That four-phase native GPU recipe also passed unchanged after aa828629, with
the same7056bytes/rates/upload equality, MSAA/AO, clean validation and normal
process exit; logs/foveation-gpu-native-gameplay-after-recovery.log.

Senior identified two initially weak assertions (stale OFF values and old input
release counts); main adopted both corrections and reran0. An initial fixture
run134 was a test-message-counter reset omission, corrected without production
changes; original log retained. The preliminary host build disables SteamAudio
only for that route; final full-feature Linux/ARM packages need refresh underF10.

Private evidence root: qsvr-final-qualification-thchgzi8, profile fixture directory
foveation-profiles-current (compile.log/run.log), logs/foveation-renderer-current-
build.log and -run.log, foveation-recovery-host-build.log; foveation-recovery-gpu-
current/probe.gdb/run.log/result.json and baseline/recovered-mirror.png. Original
private failed attempts remain. No user assets/config/global runtime or main
branch changed. The open checklist stays displayed in KWrite.

Completed subsets: profile construction and prepared renderer recovery plus
native loaded-scene recovery. Device capability/preference/selection distinctions,
protected GPU output and remaining constructor-failure boundaries stay within
F06 until their evidence is reconciled. Real FB/META/gaze/headset qualification
remains user-deferred. This is focused senior review, not final F10 signoff.
[Before-code plan and senior dispositions](foveation-recovery-final-2.0-plan.md).
