# Reuse physical density-format properties in the frame loop

2026-09-30; bounded performance adapter before code on 2.0. Keep existing native
GPU/renderer owners, graphics quality and two-view foveation. Quad views remain
excluded; FB/META qualification/default policy is unchanged.

## Verified hot path and reference

`GL_BeginRendering` calls `GL_DensityFoveationRequestedActive` every frame.
With density offsets enabled, that calls `GL_DensityOffsetFormatsSupported`,
which performs three physical image-format queries at 1x MSAA and four at MSAA:
resolve color, optional MSAA color, depth with the current AO usage, and RG8
density. Earlier sample-selector extraction removed a per-frame sample query,
but these remaining property queries still run. Source: `gl_vidsdl.c:1582–1607`,
`4210–4228`, `5172` at production commit 2205b59c.

The [Vulkan query](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceImageFormatProperties.html)
returns physical-device properties for format/type/tiling/usage/create flags.
This helper fixes type, tiling and offset create flags; samples and dimensions
are subsequently checked against the returned sample mask, maximum layers and
extent. They are not query inputs. Native device initialization already owns
physical-device selection and format policy. This is capability reuse, not
evidence about runtime-owned density-image metadata or measured frame times.

## Smallest change and ownership

Production write set `Quake/gl_vidsdl.c` only, under 75 added lines. Retain the
query helper and its caller checks. Keep a bounded five-entry cache of physical
query results/properties indexed by format and usage, reset at the start of
`GL_InitDevice` before any physical-device selection or qualification. This
handles same-GPU/new-instance or recycled handles without introducing a device
generation. It is local renderer capability state, not a resource/pass cache.

Five entries cover the current resolve/MSAA color, depth with and without AO,
and density usages. A cache-capacity miss queries normally rather than evicting
or refusing a format. Cache `VK_SUCCESS` and `VK_ERROR_FORMAT_NOT_SUPPORTED`
only; transient or unexpected query failures remain uncached. Never inspect
properties from a failed query. Preserve feature/header guards for other builds.

Reevaluate samples, array layers and both extents on every call using the
current arguments. Changed AO usage has a distinct key; resizing, mode/FSAA
changes and swapchain recreation reuse physical limits without freezing a
previous activation result. Do not cache final eligibility or borrowed images.
No render-pass, profile/gaze, graphics settings, device reconstruction or default
backend changes. A frame-result cache would need more invalidation and could
hide unsupported new dimensions; copying native allocation/passes is rejected.

Main owns this plan/index and review. One web worker owns the production file;
it must not revert others' changes. Main source review and bounded requested
local Astra advisory cover key completeness, device-lifetime reset, error
handling, capacity miss and sample/extent/AO reevaluation. Missing evidence
must be reported instead of growing another graphics owner.

No builds/tests/compiler/engine probes/fixtures/benchmarks now. At consolidated
Linux/ARM verification, count real helper queries for unchanged frames, changed
AO, FSAA and dimensions; compare eligibility and negative oversized/sample
cases with the uncached reference, and verify recreated devices requery. Only
the user's later measurements can establish a performance gain.

## Source integration checkpoint

One bounded web worker implemented the existing-helper cache and native-device
reset in the planned file (46 additions, 2 deletions). Main reviewed the full
diff and independently checked query inputs, fresh predicate evaluation,
guarded reset and native instance/device/restart ordering. The requested local
Astra Max advisor verified actual source and reported no actionable introduced
P1/P2, including result initialization, failure-property refusal, full-cache
misses, AO keys, current extent/sample checks and synchronous caller ownership.
Adopt the patch; no subsequent production correction was required.

Effective model/effort metadata is unexposed, so this is bounded requested-Astra
source acceptance, not a certified senior-skill pass. Scoped whitespace review
passes. No build, compiler check, runtime probe, fixture, benchmark or test ran.
Query-count execution, Linux/ARM qualification and measured performance remain
deferred. Borrowed density-image metadata/readiness and automatic FB/META
preference remain separate unfinished migration requirements.
