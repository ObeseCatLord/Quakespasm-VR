# OpenXR late attachment and instance recovery on vkQuake's device

Status: verified design draft before production edits; Astra disposition pending.
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
| Add original-Vulkan qualification at the existing backend; remember actual renderer creation metadata and prepare supported optional graphics capabilities | Reuses every session/frame/input/asset/render owner. The demonstrated gaps are creation provenance, missing enabled-extension records and missing device-time multiview readiness. | Preferred first implementation, subject to Astra. Additional graphics dispatch only; no parallel session machine. Leaves different-GPU/device reconstruction explicit. |
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
2. Existing renderer creation owners retain selected API, explicit enabled
   instance/device extensions and actual ordinary graphics queue provenance.
   Record only after successful creation. No resource handles are newly owned.
   Hidden enable2 additions are not guessed; known enabled names form a safe
   sufficient subset. Incompatible unknown requirements must reject adoption.
3. Unless `-novr`, query/enable core multiview on a genuinely Vulkan1.1-capable
   device and keep existing six-set layout eligibility checks. Enable only
   advertised, dependency-satisfied external memory/fence/semaphore and platform
   handle candidates at startup. Keep donor GPU choice, raster/effects/settings
   and no-runtime desktop behavior. Preserve existing foveation-family exclusion;
   optional KHR readiness must not start fixed foveation. META readiness without
   an available runtime needs its own verified dependency decision.
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
