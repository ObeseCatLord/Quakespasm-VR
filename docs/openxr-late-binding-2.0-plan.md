# OpenXR late attachment and instance recovery on vkQuake's device

Status: Astra Max design accepted with the revisions below; implementation in progress.
Scope: VR-001/VR-002/XR-001/XR-002. The whole migration and user exclusions in
[scope decisions](migration-scope-decisions.md) remain intact. No GPU benchmark,
Windows/ARM build or live headset trial is required in this implementation pass.

## Outcome, references and evidence

Explicit `vr_enable 1` should discover an available runtime after ordinary
desktop startup, and rediscover a lost OpenXR instance, without discarding the
running game, network connection, texture/mesh owners or desktop graphics.
When the runtime requires a different GPU or unsupported graphics capability,
leave desktop usable and report the incompatibility. That refusal does not
certify complete different-GPU migration or actual Vulkan device-loss recovery.
Preserve the inherited VR enable/disable behavior and vkQuake desktop output.

Verified at `26a2a67b`:

- [verified: `gl_vidsdl.c:GL_OpenXRPrepareVulkan`, `GL_InitInstance`, `GL_InitDevice`]
  The current enable2 path requires `-openxr`, chooses the runtime GPU and
  creates Vulkan handles through runtime wrappers. Ordinary desktop uses the
  donor GPU selection and direct Vulkan creation. The explicit enabled-extension
  arrays and selected API version are discarded after creation.
- [verified: `vr_openxr.cpp:discover_runtime`, `load_instance_functions`]
  Discovery requires only `XR_KHR_vulkan_enable2`; it does not offer the original
  Vulkan binding queries. Session, space, actions, images, queue locking and
  foveation policy already have one reusable owner.
- [verified: `gl_vidsdl.c:GL_InitDevice`, `gl_rmisc.c:R_CreateGraphicsPipelineLayout`]
  Core multiview is queried/enabled only for an existing XR binding. The shared
  pipeline-layout helper reserves descriptor set5 when multiview is available.
  Late attachment therefore needs device-time readiness, not a late boolean
  claiming an unenabled feature. Stereo shaders/passes still require stereo_active.
- [verified: `VID_Restart`, `VID_Init`, `VID_Shutdown`, `TexMgr_DeleteTextureObjects`]
  VID_Restart rebuilds render/WSI resources on the same device. Startup failure
  cleanup owns newly-created handles before game assets; final shutdown is not
  a live texture/mesh/descriptor recreation contract. Reusing either as a full
  live device rebuild without further work would be incorrect.
- [verified: current session recovery plan and fixtures]
  An intact binding supports explicit session retries; instance loss destroys
  backend discovery/binding records while the renderer still owns the handles.
  The attempted latch prevents automatic recovery; the renderer joins and
  retires borrowed-image users at its existing boundary.

Official sources inspected 2026-09-29 UTC:

- [Original Vulkan binding](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsBindingVulkanKHR.html)
  permits application-created handles that satisfy API, required extensions and
  runtime GPU selection. It requires enabling `XR_KHR_vulkan_enable` and the
  corresponding graphics-requirements query for the same instance/system.
- [Instance extension query](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetVulkanInstanceExtensionsKHR.html)
  and [device extension query](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetVulkanDeviceExtensionsKHR.html)
  return space-delimited required names. Enumerating advertised support alone
  cannot prove that a previously-created device enabled those extensions.
- [enable2 binding](https://registry.khronos.org/OpenXR/specs/1.1/man/html/XrGraphicsBindingVulkan2KHR.html)
  requires its creation wrappers. Do not relabel desktop handles as enable2 or
  fabricate wrapper creation by returning an existing handle from a shim.
- [Khronos original Vulkan extension source](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable.adoc)
  describes the same four queue-access functions as the existing enable2 owner.
  Vulkan2 image/binding/requirements types alias their original Vulkan types.
- [Monado's graphics-extension reference](https://monado.pages.freedesktop.org/monado/vulkan-extensions.html)
  documents common external-memory/fence/semaphore capabilities and platform
  handle extensions. This informs readiness candidates, not a hardcoded runtime
  requirement list: live query results remain authoritative.
- [Valve custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom)
  documents Linux ARM64/OpenXR, EXT gaze and FB/META foveation. It does not certify
  legacy Vulkan availability on every Steam Frame/streaming runtime. Preserve
  enable2 startup support when the legacy extension is unavailable.

Unknown: legacy binding exposure/required names on each physical runtime,
different-GPU transitions, actual device health and output/performance on those
devices. Runtime support must be queried; no headset/provider allowlist.

## Adapter comparison and decision lean

| Option | Reuse / incompatibility | Assessment |
| --- | --- | --- |
| Add original-Vulkan qualification at the existing backend; remember actual renderer creation metadata and prepare supported optional graphics capabilities | Reuses every session/frame/input/asset/render owner. The demonstrated gaps are creation provenance, missing enabled-extension records and missing device-time multiview readiness. | Preferred first implementation, accepted with revisions below. Additional graphics dispatch only; no parallel session machine. Leaves different-GPU/device reconstruction explicit. |
| Recreate Vulkan instance/device and all GPU assets on every runtime rediscovery | Reuses loader data in principle, but no live recreation contract exists across textures, mesh heaps, staging, descriptors, ray resources and task owners. | Larger necessary fallback only when the minimal binding is actually incompatible; plan it separately rather than replace adjacent healthy owners now. |
| Always initialize/choose OpenXR's device during desktop startup | Can reuse enable2, but changes donor desktop selection and introduces an XR-runtime dependency; no headset/runtime may exist yet. | Reject as general desktop behavior. |
| Bind arbitrary desktop handles as enable2 / fake successful wrapper creations | Does not establish the documented creation contract. | Reject. |
| Enable every advertised Vulkan extension | Has unverified dependencies/conflicts and changes unrelated device policy. | Reject; use a small dependency-checked interop candidate set, plus actual queried requirements where available. |

Device readiness and runtime rediscovery overlap: one immutable renderer
creation record should supply both, never a second GPU owner. A supported-device
adapter is progress toward the full goal, not a redefinition of all hot-connect
or different-GPU behavior as finished.

## Proposed implementation and acceptance

1. Commit this plan; local Astra verifies source/spec then critiques the reuse,
   startup effect and extension policy. Commit adopted/adapted disposition before
   production work. Main prepares independent fixture support while review runs.
2. Existing renderer creation owners retain actual selected API, enabled
   instance/device extensions, ordinary queue provenance and relevant feature
   facts. For enable2, a getProc adapter forwards real Vulkan creation and
   copies the runtime-merged create parameters; publish the matching handle's
   record only after both XR and Vulkan creation succeed. Direct desktop
   creation records its actual create parameters after success. One singleton
   renderer metadata record survives XR State teardown, contains no destruction
   authority and is explicitly forgotten by the renderer when Vulkan handles
   are abandoned. Creation/teardown remain serialized at the existing owner;
   callback capture supports runtime calls from another thread without TLS.
3. Unless `-novr`, query/enable core multiview on a genuinely Vulkan1.1-capable
   device and keep existing six-set layout eligibility checks. Enable only
   advertised, dependency-satisfied external memory/fence/semaphore and platform
   handle candidates at startup. Keep donor GPU choice, raster/effects/settings
   and no-runtime desktop behavior. Account for promoted core dependencies,
   explicitly enable supported platform-handle names, expand fixed name arrays
   and do not invent enabled extension names. Speculative foveation readiness
   is outside this minimum attachment proof; startup foveation stays intact,
   and broader late-attachment foveation remains unfinished parent scope.
4. Backend original-Vulkan discovery and adoption query API minimum, required
   instance/device extensions and runtime-selected physical device against
   the immutable creation record, before accepting binding/session resources.
   Compare complete names, not substrings; handle growing/empty/malformed query
   outputs and bound retries. Every later original-binding attachment rechecks
   compatibility. Shared images/session/actions/foveation/queue policy remain.
5. A fresh explicit command reaches the existing frame transition. If retained
   binding eligibility is absent, rediscover/adopt instead of rejecting all
   stops. Failures retire only newly-created XR discovery/session resources,
   retain desktop handles/assets, and require another explicit command to retry.
   Reinstall the existing queue mutex callbacks after each fresh adoption and
   before attachment; XR State teardown clears their registration.
   Do not auto-restart EXITING, -novr, or a failed attempt.
6. Consolidated Linux build and production-boundary fixtures after coherent
   implementation: ordinary desktop no runtime; explicit late attachment/new
   stereo frame; lost instance→desktop→explicit rediscovery; missing extension,
   prefix-name mismatch, changed GPU/API, malformed/growing query, unsupported
   legacy extension, failed discovery/attach; original enable2/session recovery
   regressions; queue provenance/locks, input release/reference freshness and
   preserved donor desktop gates. State dispatch/driver/GPU seams accurately.
   Final local Astra review and regular commits. Live tests stay user-deferred.

Expected first slice: backend/header/renderer, existing pipeline-layout owner
unchanged; under400 new production lines. Reopen if it needs a new asset owner,
duplicates session/renderer policy, exceeds500 lines, or repeatedly repairs new
layers. Refusal paths alone or metadata counters are not vertical completion.

## Senior brief and environment

Solo maintainer; no enterprise ceremony. Rank the original-binding contract,
desktop readiness side effects and whether a full device recreation is truly
necessary. Challenge the proposed optional-extension policy; verify promoted
core dependencies and enable2/legacy coexistence rules, rather than assuming
them. Recommend the smallest end-to-end proof and deletions/simplifications.

| Fact | Environment |
| --- | --- |
| Writable worktree | `/home/obesecatlord/Documents/quakespasmvr/quakespasm-2.0`, branch2.0 only. |
| Existing dirt | User's `docs/migration-2.0.md`; untouched. |
| Host | Linux x86-64, SDL3/Vulkan development files; current Make and dispatch fixtures pass. |
| Targets | Windows, Linux, Linux ARM64, desktop/VR; Beyond2e/Monado and Steam Frame streaming/standalone. |
| Delegation | Luna6 xhigh coding unavailable in current tool model list. Main owns writes/integration; Astra review only. |
| Excluded/deferred | No Gorilla/instant stop/skyrooms/quad views. User does live/gaze/performance tests; Windows/ARM checks later. |

Review contract: read-only exact lifecycle/device-creation scope, primary spec
verification, max800words with file/line evidence and adopt/adapt/reject table.
No edits/builds/tests/nested agents. Do not re-review networking, graphics
algorithms, asset parsing or the entire185-item inventory. If compatibility is
insufficient, report the required next owner instead of authorizing a rewrite.

## Astra Max design disposition

Local reviewer Locke verified the actual owners and primary specifications.
Main spot-checked the load-bearing enable2 Appendix Q1 recommendation: an
application may supply a local `pfnGetInstanceProcAddr` to capture the actual
combined parameters. This changes the initial subset-only proposal.

| Recommendation | Disposition and implementation consequence |
| --- | --- |
| Capture actual merged enable2 creation parameters | Adapted: use a real-create forwarding shim at the current boundary; retain metadata for the renderer handle lifetime, independently of XR State. No fake creates or second GPU owner. |
| Prefer compatible-device reuse, keep reconstruction fallback explicit | Adopted: API/extensions/GPU refusals preserve desktop; they do not finish incompatible-device or actual device-loss recovery. Plan live recreation through existing owners separately. |
| One explicit graphics binding mode per XrInstance | Adopted: fresh adoption enables original Vulkan and uses its corresponding queries; startup keeps enable2 wrapper provenance. Type aliases do not establish that provenance. |
| Restore queue synchronization after rediscovery | Adopted: register the renderer's current mutex before every attachment. Explicit intent, -novr, joins, retirement and attempted latch remain. |
| Finite dependency-checked readiness table | Adapted: supported platform interop only, promoted dependencies treated accurately and arrays expanded. No guarantee for every future runtime. Foveation readiness is a separate remaining slice. |
| Real device-time multiview and existing layout owner | Adopted: query and enable actual core1.1 multiview plus two-view/six-set limits. Verify real desktop and stereo layouts; stereo output still requires stereo_active. |
| Vertical proof beyond hand-set flags/refusals | Adopted: exercise real creation capture, existing layout/command/session/retirement boundaries and submitted stereo continuity. Describe simulated XR separately from software Vulkan execution; no claim of live HMD validation. |

The shim implementation must preserve real Vulkan results, allocation callbacks
and driver dispatch. Runtime-added API/extension/queue/feature facts are copied
before borrowed create pointers expire and published only for the returned
successful handles. A failed wrapper must not publish adoption eligibility.

Primary source for the capture correction:
[enable2 Appendix Q1](https://raw.githubusercontent.com/KhronosGroup/OpenXR-Docs/main/specification/sources/chapters/extensions/khr/khr_vulkan_enable2.adoc).

## Implementation checkpoint (Linux, 2026-09-29)

- Real-create getProc capture copies actual runtime-merged names/API/queues and
  density/multiview feature facts; matches the wrapper's returned handle and
  publishes only after XR/Vulkan success. Creation record survives XR teardown
  and is forgotten at renderer handle abandonment. Astra's implementation review
  found mutable dispatch selection and missing-resolution hazards; forwarding is
  now pinned to the wrapper's instance, validated before XR and accommodates a
  device-create shim cached during instance creation. Foreign instance lookups
  keep their genuine dispatch. Threaded/cached/multiple-create checks pass.
- Fresh original-Vulkan discovery uses corresponding queries, exact enabled
  names with bounded size/retry/parsing, both API and physical-device support,
  and the runtime GPU. It shares session/frame/actions/images/retirement.
- Desktop enables supported core1.1 multiview and advertised finite external
  memory/fence/semaphore candidates with FD/Win32 names. Donor GPU selection,
  startup enable2 foveation and all desktop effect settings remain at their
  existing owners. Name arrays expand to accommodate these additions.
- Explicit command schedules adoption at the joined frame boundary, restores
  queue callbacks and retains the attempted latch. No automatic resurrection;
  -novr remains authoritative. No late speculative foveation selection.

| Check | Result and limits |
| --- | --- |
| Linux SDL3 Make | Pass. |
| Actual creation forwarding fixture | Pass: merged facts, matching outputs, cached/foreign/threaded dispatch, missing entry points, wrapper failures and lifecycle. XR/driver spies. |
| Actual headless Vulkan creation | Pass with SwiftShader: real instance/device/core multiview, runtime-added supported names via simulated XR wrappers. No session/HMD/draw. |
| Original Vulkan boundary and retained-session recovery | Pass, actual backend owners with simulated runtime/driver. |
| Fresh discovery/adoption/session/frame/loss/rediscovery | Pass, actual backend owners; SDL loader/XR/driver simulated. Retained created handle identities and fresh sample/submitted layer. No loaded assets/scene or real XR image work. |
| Renderer command/frame transition/retirement | Pass, actual production owners; discovery/input/camera and empty prepared idle GPU resources are seams. Explicit failed/successful adoption and -novr included. |
| Real donor descriptor/pipeline layout owners | Test implemented, skips77 on this host: accessible SwiftShader exposes only four sets, while donor world/MD5 needs five and stereo needs six. Do not bypass the capability gate or claim a pass. Default hardware Vulkan driver is inaccessible. |

Remaining: actual six-set layouts and
loaded-scene continuity proof on an accessible capable driver; broader migration
closure; late foveation readiness; existing-owner live reconstruction for changed
GPU/API/extensions and actual Vulkan device loss. Live headset/gaze/multiplayer/
performance testing and Windows/ARM checks remain deferred per user scope.
This checkpoint does not declare VR-001/VR-002 or the whole migration complete.


Post-checkpoint guard: actual forwarded Vulkan API cannot be a variant or lower
in major/minor than the renderer's request. This protects preselected core entry
points/features before device initialization; the lower-forwarded-API fixture
rejects without publishing metadata while retaining any returned handle for
renderer cleanup. Linux build and real/spied creation checks pass after this
change. Extension-parser zero/oversized/endlessly-growing/invalid-character
cases also pass ASan/UBSan. LeakSanitizer cannot run under this execution
sandbox's ptrace context, so that run disables only leak detection; do not
claim leak checking. Real layout proof remains a recorded skip, not a pass.


Final review corrections in progress: Astra Max accepted the late-binding
provenance/readiness/explicit-intent/synchronization/retirement source boundaries
and the API guard, but found two capture defects. Multiple successful instance
creates could disagree with the cached dispatch pin; publication now requires
exactly one successful instance record matching both output and pin. Recycled
device handles could select a destroyed earlier creation; device matching now
searches newest to oldest. Focused regressions reject ambiguous instance creation
and distinguish the latest reused-handle device's feature facts. The creation
and actual software Vulkan checks pass after both fixes; final correction
verification by Astra accepted both fixes with no remaining source blocker. Known layout/scene proof limits persist.


## Final Astra Max source disposition

Local reviewer Dalton (`gpt-6-astra`, max, effective settings verified) accepted
creation/provenance, qualification, finite interop readiness, actual multiview
request, renderer intent/latch, queue callbacks and retirement owners after the
following corrections. Main reviewed and tested the changes before integrating.

| Recommendation | Disposition |
| --- | --- |
| Remove lookup-driven dispatch replacement | Adopted: forwarding pointers are resolved/pinned before XR; foreign-instance device lookups are forwarded unchanged. |
| Handle cached creates and missing entry points | Adapted: successful instance capture establishes its instance-scoped device dispatch for early caching; each wrapper refreshes and validates required dispatch before calling XR. |
| Reject ambiguous successful instance captures | Adopted: exactly one successful instance record must agree with output and dispatch pin; preserve any output for renderer cleanup on refusal. |
| Capture latest metadata when a device handle is recycled | Adopted: reverse-search successful matching device records; fixture distinguishes later feature facts. |
| Retain the lower-forwarded-API guard | Adopted: variants and lower major/minor are rejected before publication; renderer prepared core features/entry points retain their contract. |
| Separate source acceptance from missing graphical proof | Adopted: six-set actual layout and loaded-scene preservation remain unproved on this host, not inferred from simulated frames or empty resources. Broader implementation scope stays open. |

Final reviewer conclusion: no remaining source blocker in this bounded slice.
Linux build, actual/spied creation, command-owner, original Vulkan boundary,
retained-session and fresh-discovery/recovery checks pass after corrections.
ASan/UBSan parser checks pass with leak checking unavailable as documented.
Real software device creation passes; the real layout-owner test remains
skip77. No live headset/gaze/multiplayer/performance, Windows/ARM or complete
VR-001/VR-002/whole-goal completion is claimed.
