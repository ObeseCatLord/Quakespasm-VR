# Native exceptional/common alpha comparison

2026-10-02. Existing F05 only; production Vulkan/OpenXR untouched.
[Verified local Astra dispositions and before-code plans](stereo-alpha-reference-2.0-review.md).
Main reviews Luna's fixed-target capture recipe, corrects an invalid GDB/C
startup expression via the worker, then integrates the native consumer observer.
The worker owns only the recipe; main owns input refinement, observations,
GPU execution, analysis and integration.

Four current group/target-eye runs pass64 phases,32core states with two repeats.
All engine/GDB exits0 and exact terminal markers. Independent main decoding
finds every B/E/W/C same-state repeat exactly equal, and every exceptional/common
pair exactly equal, across both eyes and both reversed wet/dry arrangements.
Head/target pose/FOV, center clip, target eye clip, time, effective transfer,
quality and extents are invariant; only the other controlled view is co-located.
Native masks1/2 change to3/0 without mask/category/result assignment. Native
runtime validity/tracking flags are retained. MSAA4/stereo SSAO1/OIT0 and explicit
foveationoff stay unchanged.

The actual alias consumer reads mapped160-byte shader UBO contents by native
descriptor/dynamic-offset pair. Common calls have no descriptor override and no
excluded eye; exceptional calls use distinct native eye pairs with target/other
exclusion flags0/1. Literal inherited expectations independently assert wet eye:
dry entity before water, wet after; dry eye reverses that order. Native fixture
identity/leaf membership and positive four-triangle alias acceptance are checked.
Enabled phases contain exactly four exceptional or two common accepted tuples,
both target-visible categories; disabled phases contain none.

Native snapshot/presentation receipts pass120distinct snapshots per target,
480total within their respective processes. Read-only native notification expiry
plus eight consecutive matching/expired end-frame observations gate captures.
The first attempt is retained: E/W/C already match, but the first B image still
has3064different notification pixels. Capture-time expiry alone was insufficient
for a previously recorded UI frame. No cropping/tolerance/state forcing accepts
that failed repeat. Fresh settled runs resolve it; all images are inspected as
actual rendered world/model/water/HUD output, not a mock image producer.

The original12x1fixed planes provide no conservative same-side water/model
footprints in four cells. The planned minimal refinement uses12x1.5planes with
same native origins, center categories, topology, UV/color/alpha and308-byte
format. Both models stay entirely on their respective water side. Native
geometry exports agree across runs:310water polygons/1646vertices, two actual
parsed aliases. Captures/older assets remain immutable and privately retained.

Private evidence roots:
- qsvr-alpha-reference-7x48bhte: failed initial background repeat retained.
- qsvr-alpha-reference-settled-2i11vn8k: group0eye0, current assets and aggregate receipts.
- qsvr-alpha-ref-g0-e1-5v6s93hn: group0eye1.
- qsvr-alpha-ref-g1-e0-5t6n2tk6: group1eye0.
- qsvr-alpha-ref-g1-e1-jdi9_nf2: group1eye1.

Each current root retains recipe/build-host link, native geometry/layers/16PNGs,
entry/log/terminal receipts and main native-reference receipt. Settled recipe
SHA2561ce6a561056b9349e982d66ae18a2d3b99bada9412fdcebfff89af7bb746389f;
native geometry SHA2563f1a83234635836ac7008496c7f1f860c72f809e247c8f3aa253b4e4c27f660a.
Native fixture binary remains13b7d270b66aea8812edf064ab29f505d18f38092a02e93cda8c008943597753,
the bounded test host described in [native export evidence](stereo-alpha-geometry-current-2.0-results.md),
not a new complete shipping build. No hardware/benchmark/validation-layer pass.

Category-specific geometric overlap and local B/E/W influence checker integration
remain pending at this document's initial commit. Exact equivalence certifies
this selector/execution comparison only: both paths share rendering/material
policies. Independent source ordering plus native consumed flags strengthen it,
not a universal renderer or driver certificate. Successful native present calls
are observed; X11 capture has no explicit per-image completion feedback.
The isolated owned Monado service stops normally after all native runs; bounded
kernel-log observation finds no new NVIDIA fault/reset events. No system/driver
changes, unrelated process termination, physical headset input or global settings.
Other F05 boundaries and frozen checklist owners remain open.
