# Runtime foveation preference: application interoperability decision

2026-09-30. Reopens only the development gate in
`openxr-foveation-selection-2.0-plan.md`. Solo maintainer, no new runtime,
renderer, metadata query or headset allowlist. Complete goal remains migration
of inherited behavior onto vkQuake. Two PRIMARY_STEREO views; no quad views.
Main owns this brief and integration; production stays on2.0. User-owned
`migration-2.0.md` is untouched. No builds/tests/probes until implementation ends.

## Verified environment and evidence

| Fact | Evidence / qualification |
| --- | --- |
| [verified: current source] Native feature selection at `Quake/gl_vidsdl.c:2147–2180` requires `-vk-runtime-foveation` before FB/META wins. Actual candidate already checks runtime eye/META flag support, GPU FDM/non-subsampled/multiview/RG8/offset formats and native samples. | Gate is unfinished product policy, not a user setting requirement. Readiness is independent of eye-mode activation. KHR and FDM features are selected exclusively. |
| [verified: current source] Density views (`gl_vidsdl.c:4140`) use RG8, two layers and flags0. Density scene (`r_passes.c:1421`) declares initial/final FDM_OPTIMAL. `vr_openxr.cpp:1743` requires acquired/waited images before update/query; `gl_vidsdl.c:5283` does it before allocating render tasks. | Same owner, one logical frame; static host-read view means GPU barriers alone cannot prove producer readiness. No application metadata query exists for those auxiliary images. |
| [verified: current source] XR return handles/extents are checked; failed optional creation retries ordinary swapchains. Views retire before XR images. Profile failure restores off or aborts/restarts with ordinary passes. Eye validity/toggle/focus and fixed explicit-only policy remain separate. | Keep these guards and conservative eye-union culling. Successful view creation is not actual image metadata attestation. |
| [verified: official docs] [Valve custom-engine guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom) recommends the six FB/META extensions and EXT gaze, plus standalone Linux ARM64. [Valve Godot guide](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/godot) directs developers to its integration. | Vendor recommends an application interface, not private runtime allocation inspection. No local Frame execution is claimed. |
| [verified: official spec] [FB Vulkan extension](https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/fb/fb_foveation_vulkan.adoc) promises a compatible paired image and extent, but omits explicit format/layers/layout. [Issue102](https://github.com/KhronosGroup/OpenXR-Docs/issues/102) records that ambiguity. META additional flags explicitly describe swapchain images, not auxiliary allocation. | Normative universal RG8/two-layer/offset/readiness guarantees remain unproven. No fabricated query or runtime-name gate closes this gap. |
| [verified: pinned application source] Godot `cd9c5d57fb9795886f3bfed8e2003062e1378178`, `modules/openxr/extensions/platform/openxr_vulkan_extension.cpp:427` imports paired RG8 density maps with swapchain array size; driver `rendering_device_driver_vulkan.cpp:2495` creates borrowed views with flags0. | Concrete application convention matches our format/layers/static view. It is interoperability evidence, not a normative metadata guarantee. Local reference copies are under `/tmp/quakespasm-foveation-reference-cd9c5d57`; production must not depend on them. |
| [verified: pinned application source] Godot `openxr_api.cpp:187–298` acquires/waits before exposing the indexed density texture. FB extension `:209–241` updates then queries centers; Vulkan driver `:5661` obtains those offsets during render-pass end recording. Driver enables dynamic FDM when available (`:1391`), but its imported view still has flags0. | Do not copy the whole graph or infer a dynamic view from enabled features. Need distinguish convention from synchronization proof, especially profile changes overlapping prior submissions. |
| [verified: developer primary report] Merged [Godot PR112994](https://github.com/godotengine/godot/pull/112994) reports Quest Pro rendering after offset validation fixes. Its author assumed the runtime map carried the offset bit and did not need application recreation of it. | Tested public application precedent strengthens compatibility, but does not expressly settle all missing guarantees or certify Frame/Beyond. |
| [unknown] Exact runtime auxiliary creation flags, incoming layout/ownership and producer-completion mechanics. | User's live tests are outside this goal. Do not invent those facts or require private implementation inspection as the only possible qualification. |

## Main's proposed decision and real alternatives

Lean: accept the established application convention as an explicit
interoperability policy, retaining concrete capability checks and normal XR
acquire/wait/update/query/submit/release ownership. Remove the development switch
from normal complete-candidate selection; prefer FB/META for a discovered eye
runtime with complete native graphics support, retain KHR otherwise. Explicit
fixed can use its existing FB path; fixed is never automatic fallback. Ordinary
desktop without a discovered runtime retains KHR first where available.
This would finish selection policy without claiming normative metadata proof,
measured performance or actual device compatibility. Source completion should
not be equated with universal runtime certification.

Open decision: is that lean sufficient given the concrete public implementation
and vendor recommendation, or is there an actual application-side mismatch that
must be corrected first? Rank the missing facts by what an application can
observe/control. A necessary narrow layout/view/profile-order correction is in
bounds; any chosen new feature must have a verified contract and amended plan
before coding. Do not replace the missing guarantee with a speculative barrier,
GPU stall, discard-from-UNDEFINED, or flag inferred from successful view creation.

Alternatives: keep the gate until an explicit vendor statement (honest but leaves
requested automatic FB/META preference unfinished); add dynamic/deferred views
(may improve timing, but only if actual feature/view contracts solve the ranked
issue); own a new map generator or import Godot renderer (rejected: substitutes
policy or duplicates working owners); switch devices after session creation
(rejected here: separate user-deferred incompatible-device reconstruction).

Scope estimate for selection-only decision: under50 production changed lines
in `Quake/gl_vidsdl.c`, documentation/index updates separately. Reopen if review
requires another producer/resource/state owner or a material graphics change.
End-of-implementation software matrix covers exclusive candidate selection,
startup/mode changes, complete/partial extensions, default MSAA/AO/OIT,
creation/profile failure and ordinary stereo recovery, missing/invalid gaze,
explicit fixed/off, two views and desktop. Linux/ARM then; Windows/live
headset/performance remain deferred. No executable evidence exists for this slice.

## Requested senior advisory contract

One local requested `gpt-6-astra` / `max` verify-then-critique, <=1200 words.
Verify this brief against named current/pinned source and official docs before
deciding. Challenge both unsafe assumptions and unnecessary architecture/gates;
choose the highest-leverage unresolved decision and give a concrete smallest
production disposition or specific missing primary evidence. Do not prescribe
the user's excluded hardware testing as implementation work. No edits,
builds/tests/compiler/engine probes, telemetry or nested agents. Do not re-review
networking, avatars, SSAO algorithm, culling, quad views or general device loss.
Effective settings are unexposed, so required certified senior-skill review
cannot be verified; label the result requested-Astra advisory. Main remains
responsible for final architecture and records adopted/adapted/rejected findings.
