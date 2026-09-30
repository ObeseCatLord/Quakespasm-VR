# Feature-map senior review and disposition

2026-09-20. Review scope: completeness and architecture of the 2.0 feature checklist, selected useful Ironwail/QSS-M additions, compatibility boundaries and acceptance gates. This is a documentation/design review, not an approval of implemented VR behavior.

The main agent prepared a verified brief with source pins, environment facts, evidence/unknown labels, proposed decisions, rejected alternatives and a bounded read-only contract. One Astra reviewer reused its earlier renderer-audit context; a separate earlier Astra audit supplied the network map. The final reviewer’s effective model and reasoning effort were verified by the main agent as **gpt-6-astra / xhigh** using only the relevant metadata fields. The reviewer could not verify that metadata from its own interface; main verification supplies that requirement. No session logs or environment telemetry are included here.

Verdict: keep vkQuake’s existing renderer, tasks, resources, loaders, allocator, network transport and VM owners. The inventory is sufficient to guide a **bounded integration proof after corrections**, not unrestricted bulk porting or a claim of runtime parity. Review priority: renderer feasibility/lifetime, wire/save compatibility, behavioral coverage, then optional additions.

| Recommendation | Main verification | Disposition / resulting plan |
|---|---|---|
| Restore the early task-enabled multiview proof before bulk migration. | Existing architecture/status plans already require it. Donor `Quake/gl_screen.c:1587–1599` joins drawing before the later submit in `Quake/gl_vidsdl.c:3518`; `Shaders/indirect.comp:77–83` rejects from one view origin. | **Adopted.** P1 exit requires representative opaque multiview, once-per-frame mutable preparation, lit moving brush, independent poses for two instances sharing an avatar asset, correct either-eye rejection and clean task/GPU teardown. P2/P3 bulk migration depends on this; the minimum coupled input/network slice needed for the proof is allowed. P4 completes coverage and optimization. |
| Do not use queue-submission count as proof of single-pass stereo or force transparent/UI passes into the same draw. | PERF-021 formerly said one scene submission including transparency; XR-011 already allowed per-eye exceptions. One queue submit may contain duplicated rendering. | **Adopted.** PERF-021 and XR-011 now require actual multiview for eligible opaque geometry and correctness-preserving exceptions. Reuse one acceptance gate across the overlapping rows. |
| Preserve `.jpeg` lookup and image precedence, not only decoder availability. | Fork `Quake/image.c:196` includes jpeg and lists TGA before PNG; donor `image.c:156` lacks jpeg and reverses that tie order. Both prioritize the higher search path. | **Adopted.** ASSET-001 requires `.jpeg`-only replacement and equal/unequal-priority TGA/PNG cases. Adapt the existing donor loader; preserve its additional supported formats. |
| Preserve CPU-path lightstyle interpolation. | Fork `Quake/gl_rlight.c:48–75` interpolates without a GPU-update switch; donor `:67` suppresses interpolation with `r_gpulightmapupdate=0`. | **Adopted.** ASSET-009 now reconciles this donor difference. Acceptance covers smooth/abrupt styles, modes 0/1/2 and CPU/GPU update paths with dynamic lighting enabled. No parallel lighting system. |
| Make inherited OpenVR presentation a release gate. | Fork `Quake/vr.c:9356–9359` submits OpenGL textures; retained pose/input code cannot establish Vulkan compositor compatibility. Existing plan already requires OpenVR preservation. | **Adapted to actual platforms.** P5 and PLAT-001 explicitly qualify OpenVR Vulkan presentation and controller/lifecycle behavior on Windows/Linux x86-64 with available runtimes. Linux ARM64 OpenVR availability remains unresolved; its mandatory desktop/OpenXR target is unchanged. Do not invent runtime support or silently close VR-002 through OpenXR testing. |
| Keep wire/save reconciliation narrow and explicit. | Private protocol bit/opcode collisions are recorded in NET-002; fork multiclient save version 6 collides with donor KEX version 6. | **Retained.** Existing negotiation/parser/save owners remain authoritative. Pin the legacy peer matrix and distinguish save dialects before world mutation. Broader peer compatibility is not assumed from shared feature names. |
| Group features practically but leave interface/behavior closure visible. | Main validated 185 unique work IDs, readable-table/CSV alignment, preserved source blobs, history routes, literal interface counts and local links. These do not establish runtime equivalence. | **Adopted.** Describe the result as a scope-inventory checkpoint. Preserve per-interface and runtime acceptance gates; include the 512-entry QC appendix and distinguish source anchors from mechanical path routing. |
| Keep optional additions subordinate to parity and evidence. | Candidate maps distinguish implemented upstream mechanisms, bounded absence searches and unqualified dependencies. Background-save workers touch QC context; copying their thread entry point is insufficient. | **Adopted.** Background saving follows proven co-op save round trips and immutable state/metadata ownership. Clustered lighting stays a measured experiment; the other optional priorities remain proposals. |

The review earned its cost by catching real donor behavior differences and a phase-order conflict that could otherwise permit bulk work around an unproven stereo architecture. Main spot-checked each load-bearing correction in the source before applying it.

Later product preference (2026-09-22) supersedes the ASSET-001 tie-precedence
disposition above: vkQuake's PNG-before-TGA order remains the visual default.
The `.jpeg` lookup was added through its existing loader; higher search-path
priority still wins. The historical review finding is retained for provenance,
but its proposed TGA-before-PNG change is not the current migration target.

The OpenVR-runtime release-gate row above is also historical. The later explicit
OpenXR-and-desktop targets preserve inherited behavior through OpenXR rather
than requiring a second OpenVR Vulkan compositor. The current readable map and
CSV VR-002/PLAT-001 now agree on that boundary. Current scope decisions also
defer physical-contact/Gorilla work, Windows verification and user live-device/
multiplayer/performance checks; those do not add completion gates in this pass.

No new user permission is needed to preserve the existing requirements. A proposed reduction in inherited OpenVR/legacy-peer compatibility or adoption of substantial optional scope would require an explicit product decision. No such reduction or scope expansion is made here.

Verification for this checkpoint: pinned source-anchor existence; CSV shape/unique feature IDs and readable-table correspondence; document links; preserved source/worktree state; `git diff --check`. No builds, runtime tests, hardware performance claims, release packaging, pushes or deployment were performed for this documentation pass.
