# Diagnostic frame-boundary barrier

2026-10-01. Follows the [local Astra disposition](unused-attachment-sync-2.0-review.md).
Main owns Quake/r_passes.c only; independent Luna owns sv_phys.c and the earlier
worker owns metadata tests. No overlap, main/master/user asset edits or teardown
changes. Bound25 changed lines, approximate10–20. This is a diagnostic probe,
not a production performance decision.

Insert one VkMemoryBarrier immediately before the render-pass primary command
buffer's first pass, outside any render pass: ALL_COMMANDS to ALL_COMMANDS,
MEMORY_WRITE to MEMORY_READ|MEMORY_WRITE, dependency flags0. Keep native recording,
dependencies, submissions, references, framebuffers and resource owners. No image
layout guess or queue idle. Rebuild host and reproduce identical native desktop
MSAA/OIT settings with private validation and controlled assets. Compare actual
hazards and output; a positive result establishes broad ordering suffices, not
the conflicting image's identity or acceptable production overhead.

Remove the exact probe after diagnosis. Capture results and use the existing
pass-only attachment-compaction fallback if needed, with its own committed
before-code plan and source review. Do not accept global serialization as the
final performance-oriented solution without further justification. User performance
measurement remains excluded; no timing/speedup claim or gate. Normal desktop
shutdown remains an independent finding. Future production pass changes require
affected native desktop and actual simulated-XR qualification.
