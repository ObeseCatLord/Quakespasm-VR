# Final renderer qualification: senior disposition

2026-10-01. Local `gpt-6-astra` at explicit `xhigh`, effective route checked
from its turn context. Read-only review of the
[verified brief](final-renderer-qualification-2.0-brief.md), actual production/
baseline pass and teardown owners, and local GPU logs. Main independently
checked the baseline's ten matching WAW reports and allocator-corruption
termination, and the clean24-probe simulated-XR result. This is a focused
disposition, not full-goal signoff.

The brief's pending baseline and XR entries are historical. Subsequent evidence:
untouched vkQuake `4bc898f29073e8aa41069f0e79e3cb5a9eb73afa` was archived,
built independently with the same host toolchain/options and run on the same
driver/layer. It reports ten WRITE_AFTER_WRITE hazards and allocator corruption
after client removal; ordinary process exits134. Current2.0 reproduces both
symptoms. This establishes baseline reproduction, not identical root causes
and not driver-fault attribution.

The actual current rendered simulated-OpenXR run passed all24 probes, reported
zero validation errors/hazards, and exited0. Captured two-eye output was inspected.
See [GPU receipt](final-openxr-gpu-qualification-2.0.md).

| Senior recommendation | Main disposition |
| --- | --- |
| Preserve renderer/resource ownership; reject a renderer or lifetime rewrite | Adopted. Baseline reproduction provides no evidence that the port's architecture requires replacement. |
| Identify the implicated image/view/subresource and prior/current passes before a synchronization change | Adopted. Keep the correction at the existing generated-pass or native barrier owner; no blanket queue-idle per frame. An outgoing dependency and larger dependency arrays are options only if exact access scopes justify them. |
| Keep desktop validation and normal quit as acceptance requirements | Adopted. Baseline provenance does not make either failure correct. Attach both to existing desktop/rendering qualification, without inventing new features. |
| Do not prescribe broad shutdown cleanup from the allocator stack alone | Adopted. Baseline aborts without the port's extra pipeline destruction. Determine the failing teardown boundary and whether a justified synchronization fix changes it before selecting a native cleanup call. |
| Accept the clean XR matrix within its actual boundaries | Adopted. Real Monado/Vulkan rendering is established for the tested matrix; actual headset/gaze/foveation and remaining software cases are not. |

## Required next fixes and acceptance

1. Capture exact hazard attachment and pass access mapping. Plan the smallest
   native pass/barrier correction before editing. Qualify implicated desktop
   settings and resource recreation with correct output and zero hazards,
   retaining the clean XR matrix. The senior estimate is roughly half a day
   for diagnosis/bounded patch, excluding qualification; it is not a promise.
2. Localize the shared desktop abort and repair only the demonstrated boundary.
   Reuse existing teardown functions where supported by evidence. Require
   repeated ordinary desktop quits with exit0, including resource recreation.
   Root cause and repair effort remain unknown; roughly half a day was allowed
   for localization, not completion.

No human scope decision currently blocks these steps. This review does not
waive defects or authorize new rendering/lifetime systems.
