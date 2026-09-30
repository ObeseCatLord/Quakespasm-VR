# Two-eye FB/META density offsets: reopened design brief

2026-09-30; plan before code on2.0. Main integration with bounded delegated coding, no new
renderer or runtime owner. Goal: the existing runtime foveation path follows
gaze where the supported Vulkan/runtime route uses tile offsets, while retaining
vkQuake MSAA, desktop graphics, conservative two-eye culling and full-rate
weapons/UI. Eye tracking stays optional; fixed stays explicit-only. Quad views
remain excluded. User live hardware/performance testing is outside the goal;
Linux/ARM software checks follow full implementation.

## Verified evidence versus assumptions

| Fact | Evidence and qualification |
| --- | --- |
| Current writable implementation | quakespasm-2.0 only; prior committed HEAD239c523f, source read directly. User-owned docs/migration-2.0.md stays untouched/unstaged. [verified: preceding status and current source] |
| Native owners already contain the offset machinery | gl_vidsdl.c queries QCOM/EXT features/properties, checks offset image formats, chains selected feature, flags color/depth images, computes two aligned offsets with VRF_DensityOffset. r_passes.c:1133 submits the pair at the density render-pass end. [verified: direct source] |
| Selection deliberately disables the machinery | gl_vidsdl.c:2075 sets offset feature/use-ext false even for a qualified candidate; GL_OpenXRAttach near4716 passes additional image flags0. [verified: source] |
| MSAA would violate offset attachment requirements if merely enabled | GL_CreateColorBuffer near2800 resets image flags0 for MSAA. Existing offset-format helper checks sample1 only. Offset pass uses MSAA color and single-sample resolve as well as depth/density. [verified: source] |
| Vendor implementation uses application offsets | Meta's [ETFR explanation](https://developers.meta.com/vr/blog/save-gpu-with-eye-tracked-foveated-rendering/) describes its Vulkan multiview integration using QCOM tile offsets; Qualcomm's [implementation guide](https://www.qualcomm.com/developer/blog/2022/08/improving-foveated-rendering-fragment-density-map-offset-extension-vulkan) requires every framebuffer attachment to carry the offset flag, including density/depth/color/resolve. [verified: primary docs read] |
| Valve documents a working source reference | [Valve Godot guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/godot) documents Frame foveation via the vendor integration and native Linux ARM64. [verified: official guide read; not an executed Frame result] |
| Pinned Godot applies returned centers | Godot cd9c5d57fb9795886f3bfed8e2003062e1378178, modules/openxr/extensions/openxr_fb_foveation_extension.cpp:163/209-241 requests QCOM image flag through META, updates profile then queries current centers, converts NDC to framebuffer-pixel offsets. platform/openxr_vulkan_extension.cpp:204-226 requests offsets and aligns them;427-438 imports RG8 density images with swapchain array size. [verified: pinned primary source fetched/read] |
| EXT is a compatible spelling of existing QCOM records | [Vulkan offset-end spec](https://docs.vulkan.org/refpages/latest/refpages/source/VkRenderPassFragmentDensityMapOffsetEndInfoEXT.html) aliases QCOM struct, requires each used attachment's offset create bit, two offsets for two density-view layers, device granularity. Installed vulkan_core.h aliases struct/sType/flag. [verified: official spec and header read] |
| META does not by itself settle the graphics-specific placement mechanism | Its [official source](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/meta/meta_foveation_eye_tracked.adoc) says runtime must apply eye pattern and recommends update immediately before state query. That does not negate Meta/Godot's documented offset integration. [verified: source; mechanism inference separated] |
| Borrowed image metadata is still incomplete in the XR spec | [FB Vulkan source](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/fb/fb_foveation_vulkan.adoc) returns compatible image/extent but no explicit format/layers/layout. [Khronos issue102](https://github.com/KhronosGroup/OpenXR-Docs/issues/102) remains open. Godot's RG8/layer import is interoperability evidence, not a new normative guarantee. Actual runtime layout/readiness/offset creation of the density image remains unknown without vendor/runtime qualification. |

No build/test/compiler/probe/benchmark was performed to gather this evidence.
Pinned inherited XR30808413 already supplies VRF_DensityOffset/profile helpers;
keep those instead of copying Godot's rendering device, engine threading or
replacing vkQuake's Vulkan owners. The source donor references remain readonly.

## Minimal adapter versus replacement

Reuse existing queries, feature chains, scene attachment topology, center helper,
profile validity/stability, offset array and render-pass-end node. Extend the
existing flags/query boundary for actual native MSAA color/resolve/depth usage,
request the existing META swapchain-create flag, and preserve full-rate failure
recovery. Native MSAA selection stays one policy: factor its existing selection
only if necessary to qualify samples before allocating offset images, rather
than duplicate a second FSAA algorithm. No new eye views, renderer, gaze action,
device generation, runtime image ownership or copied render graph.

The previous assumption that META profile updates always eliminate application
offsets is reopened by vendor/source evidence. Do not unconditionally enable
offsets for every runtime solely from a profile name: the placement convention
and every attachment's creation contract must be deliberate. Likewise, do not
leave an advertised eye path permanently disabled as a substitute for the user's
FB/META preference merely because final hardware tests are deferred.

## Open decisions for local Astra design advisory

1. **Placement contract.** Lean: use the existing capability-qualified QCOM/EXT
   offset route with META image-create support, matching Meta/Godot, after
   requesting the offset bit and qualifying all owned attachment formats/sample
   counts. Keep the ordinary no-offset route only if its runtime placement
   contract is established; otherwise preserve KHR/full rate, never fixed.
   Reviewer decides whether evidence supports generic capability-based use or
   requires a narrower qualification boundary without a headset-name allowlist.
   Rejected: a new gaze backend, unconditional double-shift of an already moved
   map, or merely toggling the feature without MSAA/density flags.
2. **Borrowed-image/default policy.** Reassess whether Valve's documented Godot
   route and pinned primary source establish a usable interoperability contract
   sufficient for implementation (distinct from user hardware qualification),
   or whether explicit format/layer/layout/readiness evidence is still needed.
   Current -vk-runtime-foveation gate must not silently become final product
   policy contrary to FB/META preference. Lean: implement all source-qualified
   prerequisites now, avoid fabricated metadata queries; decide final default
   from actual vendor/source contract, not from mocked handle success. A GPU
   format-capability query cannot discover a borrowed image's format/flags.
   Rejected: claiming normative guarantees from sample code, wholesale runtime
   replacement or requiring the user's excluded live tests to count source work
   as implemented. This overlaps decision1; merge where appropriate.
3. **Sample/flag readiness.** Lean: keep the donor's MSAA selector and adapt
   offset qualification to its actual sample count/usage before allocation;
   retain flags on MSAA color as well as resolve and depth, and pass META flags
   at XR attachment. Preserve current 4x quality if offset combination cannot
   work. Reviewer chooses the smallest native boundary, not a new render graph.
   Rejected: lowering MSAA, discarding protected depth replay or allowing a
   sample1-only check to qualify multisampled color/depth.

Expected first production write set Quake/gl_vidsdl.c, under150 added lines;
Quake/vr_openxr.cpp or r_passes.c only for a demonstrated contract mismatch
resolved in review. No write to main or user migration status. Reopen before
expanding beyond that bound or adding another state/policy/owner. Record advisory
disposition before code; one bounded web coding agent, main integration.

Request one local Astra Max verify-then-critique advisory, <=1200 words. Scope:
this brief and three named existing foveation owners plus helpers/official source
needed to verify the listed contracts. Prioritize the real decision, challenge
unnecessary gates as well as unsafe enablement, identify a concrete minimal
implementation and genuine human choices (if any). Do not re-review networking,
avatars, general GPU/device reconstruction, quad views or speculative subsampling
renderer work. No edits/builds/tests/compiler checks/probes/nested agents or
telemetry. Effective settings must be exposed to certify the skill; otherwise
label advisory. Missing evidence must be reported, not replaced with assumptions.

## Adopted advisory disposition (2026-09-30)

Requested Astra Max returned a source advisory; effective model/effort metadata
was not exposed, so this is not a certified senior-skill pass. Main independently
confirmed the sample selector runs after density activation, MSAA creation clears
the offset flag, and unsupported META flags are rejected before ordinary-swapchain
retry. No human choice is needed for the following bounded correction.

| Decision | Disposition before code |
| --- | --- |
| Placement plus borrowed contract | Adopt the existing application-offset route, with complete graphics/META prerequisites. Reject the assumption that profile updates universally translate Vulkan maps. Retain the temporary development gate until runtime evidence establishes density creation flags, RG8/layers, layout and producer completion. Automatic FB/META preference remains unfinished product policy, not replaced by permanent opt-in. |
| Native samples and attachment flags | Extract the existing native selector once, preserving its choices and Intel exception. Use it for initial capability qualification and before resource activation/allocation. Qualify actual single-sample color/resolve usage, optional MSAA color usage and native samples, depth usage with AO sampled bit only when requested, and density offset-format capability. Never lower MSAA to enable foveation. |
| Device selection | Require META additional-image-flags support and complete offset capability for the eye route before it wins FB selection. Enable the existing selected feature/extension chain. Explicit fixed mode may use the existing non-offset route; eye mode may not silently claim support without established placement. |
| Attachment after rediscovery | Recheck META flag support. If it has disappeared, request ordinary stereo directly; rejected optional creation still uses the existing XR retry. Request the existing offset bit when the route remains qualified. |
| Existing frame and pass owners | Keep paired validity/profile restoration, VRF_DensityOffset and native render-pass-end submission. No additional gaze action, OIT offset flags, runtime owner, eye views, or render-graph rewrite. |

Production write set remains Quake/gl_vidsdl.c, expected 100–140 added lines,
reopen before exceeding150. Main will review the patch and request a bounded
source advisory; builds/tests/probes remain deferred until full implementation.

### Remaining contract evidence

Godot's import convention and requested color flags do not attest to borrowed
density-image flags or producer completion. Vendor/runtime source or documentation
must address those specific facts before removing the development gate. Successful
view creation is insufficient. This is a bounded source-evidence task, separate
from the user's later live hardware testing.

Main's bounded follow-up read Meta's current [native FFR guide](https://developers.meta.com/vr/documentation/native/android/os-fixed-foveated-rendering/)
(updated2026-04-07), its linked archived native guide and current Unreal ETFR
guide. The native Vulkan steps demonstrate paired density-image enumeration and
render-pass use, but do not specify the missing format/layer/layout/offset flags
or producer-completion contract. The archived guide uses deprecated VrApi and
cannot establish this OpenXR contract. Default fixed fallback in Meta's engine
examples is deliberately not adopted.

The public [Khronos issue102 discussion](https://github.com/KhronosGroup/OpenXR-Docs/issues/102#issuecomment-915528895)
also asks whether profile updates modify the enumerated map. Its three published
comments contain no resolution. Primary source inventories at Meta-OpenXR-SDK
bbed2f20e38a5df7113630771c83cb8279e4fc26 and ValveSoftware/Unity
329c81f5a97a7f9e7740cf4307f1bfa9ce090b3a do not expose a density-image producer
implementation in the scoped samples/features. Valve's Unity feature delegates
to UnityOpenXR native functions; this is not evidence of creation flags or
synchronization inside the runtime. These bounded reads did not close the gap;
they do not prove that no relevant documentation/source exists elsewhere.

### Consolidated qualification

Linux/ARM software checks: actual feature-chain selection and QCOM/EXT aliases;
image creation/retirement with all density-pass attachments at 1x/default4x,
both OIT variants and AO; native sample settings preserved; paired aligned center
conversion, fixed zero offsets, invalid/unavailable gaze selects off, and ordinary
stereo retry after rejected optional flags/profile. End-to-end GPU validation and
off/on/off center movement remain actual device work, not counter/mocked proof.
The user performs live headset/gaze/multiplayer/timing tests separately; no
Frame/Beyond compatibility or performance gain is certified by source alone.

## Source implementation checkpoint

The bounded Quake/gl_vidsdl.c patch reuses native device and resource owners:

- GL_SelectNativeSampleCount contains the original FSAA16/8/4/2 choices and
  Intel16 exception, with a defined1x result if its format query fails. Device
  qualification and resource creation use that one selector. Ordinary frame
  entry consumes the existing sample_count; it adds no per-frame selector query.
- GL_DensityOffsetFormatsSupported qualifies actual resolve, optional MSAA color,
  depth/AO and density usages with the offset flag and actual native samples.
  Resource activation also checks real scene/density extents. This checks GPU
  support, not borrowed-image metadata or creation attestation.
- The complete startup eye candidate requires META image-flags support. A
  selected FDM device enables its qualified offset capability independently of
  mutable fixed/eye mode. Explicit fixed with no offset candidate remains possible.
  No-KHR device preparation under the development switch is retained for later
  runtime discovery; this preparation does not promise late eye-offset support.
- MSAA color creation preserves the offset flag already set for scene/resolve.
  Existing depth flags, two-eye pass-end submission and signed conversion remain.
  No OIT attachment, shader, render graph or desktop graphics algorithm changed.
- GL_OpenXRAttach rechecks current META/eye support and requests the offset bit
  through the existing XR owner. Capability loss requests ordinary stereo before
  its flag precondition; optional creation rejection retains the existing retry.
  Eye activation requires enabled offset capability, never automatic fixed mode.

One bounded web coding worker produced the patch; main reviewed the diff and
corrected unnecessary per-frame native format querying, retained late device
readiness, and retained offset readiness across fixed-to-eye policy changes.
Production commit92b68e9d is107 additions/73 deletions in the one planned file.
Scoped git diff --check passes. Final requested-Astra source advisory found one
P2 at device selection: when both backends qualify, a saved foveation-off setting
selects KHR permanently and prevents later META activation without restart. Main
confirmed the mode predicate in prefer_fb_eye and the exclusive feature selection.
Adopt the bounded correction: remove that eye-mode predicate from capability
preparation, preserving the development gate, actual complete candidate and all
runtime activation/toggle checks. This is capability readiness, not automatic
foveation activation. No other P1/P2 was reported for92b68e9d. Effective review
settings remain unexposed; source advisory only. No build, compiler, runtime,
fixture, benchmark or device check was performed.
Automatic FB/META preference still requires the explicit remaining contract
evidence above; this checkpoint does not close that release blocker.

## Final requested-Astra source advisory

After main's one-line correction6a519ade, the same local advisor confirmed the
P2 closed, with no additional issue in that correction. Its preceding full slice
review found no other P1/P2. Main spot-checked the corrected candidate selection,
native sample callback/resource ordering and the load-bearing primary contracts.
This is source acceptance for the bounded adapter, not executed GPU qualification
or a certified effective-model/effort senior-skill pass. All agents are closed.

The completed bounded contract investigation found no inspected public primary
source explicitly closing density-image offset flags, incoming layout/queue
ownership and producer completion/stability. The [META swapchain-create contract](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/meta/meta_vulkan_swapchain_create_info.adoc)
defines additional flags and unsupported-request rejection, without explicitly
extending the flags guarantee to the auxiliary density map. The [KHR Vulkan
layout contract](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable.adoc#L389)
defines color/depth swapchain handoff, without explicitly defining auxiliary-map
handoff. Static density consumption may begin during host command recording;
GPU submission ordering alone does not establish its readiness then.

Pinned Godot's [native texture import](https://github.com/godotengine/godot/blob/cd9c5d57fb9795886f3bfed8e2003062e1378178/drivers/vulkan/rendering_device_driver_vulkan.cpp#L2495)
creates an image view over an existing image, with view flags zero. Its RG8/layer
convention, graph transitions and offset integration are supported source
precedents, not density creation or producer-completion attestations. View flags
are distinct from the image creation offset flag.

Do not demand private runtime allocation/fence inspection or infer runtime
nonconformance. A supported application-level contract is sufficient: seek a
vendor/runtime native example or explicit statement covering
acquire -> wait -> update/query -> record -> submit -> release, including the
map's flags, layout/ownership, permitted transitions and stability during
overlapping updates/host/device consumption. This is the next specific evidence
boundary; a generic user hardware-test gate or fabricated query is not a fix.
The temporary development gate therefore remains explicit and unresolved.

Only scoped whitespace/source checks were performed in this implementation
checkpoint. Linux/ARM builds and software qualification remain deferred until
the complete implementation pass; live device/gaze/performance tests remain the
user's separate work. Quad views remain excluded.
