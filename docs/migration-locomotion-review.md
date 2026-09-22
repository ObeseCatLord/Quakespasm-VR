# Analog locomotion and local turning increment

This work ports inherited joystick locomotion and local turning into vkQuake's
existing input, pending command and tracked-view owners. The integrated Linux checks described below pass. It does not complete P1 or provide controller
muzzle positions, roomscale, weapon presentation/wheel, haptics, VR-specific
swimming/ladder authority, or device qualification.

## Source and architecture

The behavior reference is `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`,
`Quake/vr.c:11282–11443`: strongest-axis filtering, head/offhand/raw movement,
near-vertical basis handling, forward speed for both horizontal axes, vertical
movement from the original selected forward vector, and snap/smooth/180 turning.
Controller-angle composition is `vr.c:10254–10264` and `mathlib.c:366–393`.
The existing `VR_AimPoseAngles`, native `AngleVectors` and `R_ConcatRotations`
remain reused. The small missing matrix-to-angle helpers are private to the
ported arithmetic module. `Mod_Weapon` changes model transforms, not offhand
movement orientation.

Transplanting the inherited `VR_Move` and command loops would duplicate vkQuake's
working sampling, duration, journal and prediction-preview owners and import
unrelated renderer/weapon dependencies. The narrow design samples the completed
XR frame once after `IN_Move`, stores a derived VR contribution in the existing
`cl.pendingcmd`, and applies that contribution through one nonconsuming assembly
helper. Native pending consumption clears VR velocity; same-frame catch-up may
retain the prepared angle basis. These local fields are not serialized by any
wire codec and do not create a second command clock or simulation.

A single WebGPT xhigh worker ported pure math; its focused fixture linked actual
native math and passed. A subsequent single WebGPT worker owns the input adapter.
No two WebGPT workers run concurrently. Local Astra/Max provided design review;
the orchestrator verified effective model/effort and spot-checked its source
claims. A Luna/xhigh worker supplied packet regressions and a native probe.

## Reuse boundaries

The supplied Skyrim VR architecture postmortem remains the governing reuse rule:
engine/runtime incompatibility justifies an adapter at that boundary, not a
replacement gameplay or networking system.

| Existing component | Actual incompatibility | Smallest adaptation used |
| --- | --- | --- |
| Inherited VR movement and controller-angle math | OpenVR controller structs and adjacent renderer dependencies | Port the pure calculations; pass mapped poses/axes from the existing OpenXR boundary. Reuse native vector/rotation primitives. |
| Native SDL/keyboard input and command builders | Need an additional tracked-controller contribution | Add one post-sampling adapter and one shared nonconsuming assembly hook; preserve native bindings, duration and finalization. |
| Native pending command | Mixed axes alone cannot discard VR input after a server correction while retaining desktop input | Keep the derived VR contribution inside the same pending-command lifetime, separately until assembly. |
| Existing tracked-view yaw resolver | Origin rebase can overwrite a just-accepted local turn | Retain an uncommitted yaw delta in that existing owner and apply it after rebase. No second tracking or camera owner. |
| Existing public/private command codecs | Public writer ignored the command's prepared angle basis | Change the two public angle writes; preserve existing wire formats and journal. |

No replacement runtime sampler, transport, simulation, prediction journal,
movement service or renderer was introduced for this increment. The next port
must identify its reusable owner and demonstrate a specific incompatible call or
data boundary before expanding the replacement scope.

## Senior review dispositions

| Recommendation | Disposition |
| --- | --- |
| Read the completed frame after command-buffer execution; do not add a runtime sampler/cache | Adopted. Existing digital acceptance/context gates must be revalidated after native commands can change context or refresh XR. |
| Keep sampled movement and command angles together until native assembly | Adopted in `cl.pendingcmd`; no separate lifetime or duration owner. A server correction between accumulation and preview must discard VR motion without deleting desktop input. |
| Both public and private writers must use prepared command angles | Adopted. The public writer previously read `cl.viewangles`; its angle widths and protocol layout stay intact. |
| Keep an uncommitted local turn in the view owner until normal resolution | Adopted. Effective movement mapping includes that delta; view resolution applies it after an origin rebase, once. |
| Absolute server angles supersede local turns; relative angles compose through the existing owner | Adopted. Both invalidate derived movement. Native angle locks suppress VR movement/turning and require neutral rearm. |
| Gate movement channels by their actual pose/action dependencies | Adopted for implementation. Head movement does not require offhand grip orientation. Hand/raw vertical movement does; raw mode 7 additionally needs dominant-hand command orientation. Pose loss must not disable unrelated button navigation. |
| Preserve RAW's dominant-hand command basis in controller aim mode | Adopted as a small orientation producer, without pretending that grip poses are complete weapon/muzzle data or setting `vr_active`. |
| Scale only the VR contribution | Adopted. Preserve analog speed and the vanilla-run compatibility control; avoid the inherited second multiplication of already-scaled keyboard movement. |
| Guard finite but unencodable movement near singular orientations | Adopted for implementation; reject an invalid VR contribution before packet conversion. |

The source computes head movement command angles after turning but can reuse
pre-turn cached hand angles. A consistent post-turn sampled basis is an explicit
adaptation. Fresh offhand movement angles in aim modes 1–6 also avoid stale source
hand-angle history. These are targeted corrections, not claims of exact behavior
for source bugs. Fixed foveation and eye-tracking policy are unaffected.

## Acceptance still required

Consolidated Linux build, existing affected regressions, actual native
movement/server displacement and rendered yaw, all three movement modes,
independent head/offhand/dominant orientations, mixed input speeds, pose/focus
loss and rearm, snap hold/reversal, smooth timing, queued 180, repeated previews,
send/no-send/catch-up, absolute/relative authority and reference rebase.
Packet checks must deliberately distinguish global from prepared angles and
consume the full real public/private packet. Fixtures and simulated Monado do
not qualify physical controllers, performance, Windows or ARM64.

## Final review findings and local checks

The final local Astra/Max pass accepted the owner split and found no additional
P0/P1 production defect in this bounded increment. Its profile-isolation finding
was adopted: a role change invalidates both channels, while a profile-only change
uses the existing per-hand gate. HEAD locomotion continues while only the
dominant profile changes; dominant-stick turning continues while only the
offhand profile changes. RAW controller-mode movement still observes its
dominant-orientation dependency. Both held-channel regressions pass.

The review also identified incomplete native-probe setup: a missing speed-stage
plan, an unarmed preview probe, incorrect physical hand selection during turning,
and absent neutral samples after changing snap settings. Those test defects were
corrected before execution. The probe uses synthetic controllers because the
isolated Monado configuration supplies only an HMD; rendering and gameplay still
use the real engine/runtime owners. Its camera probe observes the adjusted
stereo camera before native restoration, not the restored desktop base.

The public/private prepared-angle correction was separately committed as
`36bfa39e`. The Linux build and arithmetic, actual codec, native desktop
preview/timing and controller-adapter checks pass. Input and preview checks use
ASan/UBSan. The actual camera-owner fixture passes turn/rebase/authority checks
across all seven aim modes. The first new absolute-yaw assertion omitted the
source's visible-weapon prerequisite; supplying that prerequisite fixed the test,
without changing production authority policy. Fresh `vr_turn180` commands are
admitted only during eligible gameplay, preventing a disconnected console
request from executing after a later connection. Its affected checks and the
profile-isolation checks pass after the final production changes.

The initialized local `e1m1` smoke passes through actual command assembly,
transport, QuakeC/server collision and XR rendering. It observes movement in all
three modes and both handedness choices, distinct head/offhand/dominant command
orientations, focus-loss neutral rearm, and additive desktop/VR run scaling.
Actual rendered turns are -30 degrees, +30 on direct reversal, 180 degrees, and
continuous smooth turning. Five repeated previews at the sender and five on a
no-send frame leave native input/client state unchanged.

Using native scheduling cvars, the probe records 38 no-send frames retaining VR
input, three sends consuming it, and 12 catch-up frames retaining command angles
while consuming VR velocity only once. These are correctness observations under
a debugger, not latency or performance measurements. The isolated runtime and
all test clients were stopped afterward. Absolute/relative authority and origin
rebase are checked by the camera-owner fixture, not by a live peer injection in
this smoke. Pitched swimming, full VR-specific swim/ladder behavior, physical
controllers and additional platforms still require later qualification.

The follow-up local Astra/Max review independently matched the native pass record
to its result JSON and closed both previous findings. It accepted this bounded
increment with no remaining blocking finding and found no unnecessary new
production layer: native pending consumption, QSS timing and tracked-view yaw
remain the owners. This acceptance retains every hardware, platform, live-peer
and full-P1 limitation above. The implementation checkpoint is `7dcb1b4d`.
