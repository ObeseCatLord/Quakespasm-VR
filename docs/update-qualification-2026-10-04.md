# Upstream, anisotropy and Bonk update qualification

All implementation is committed on `2.0`. Official vkQuake HEAD was confirmed
as `749b4fd43fe8b8554b34163dc9469c4a08a4def1` after integration; merge
`59c4df5c` retains that upstream parent and all 36 incoming commits.

The final Astra/xhigh review found no source blockers at `a5792be6`, following
the verified Bonk callback guard repair. Subsequent changes add only the final
Bonk fixtures and qualification records. Source was independently reviewed by
the main integrator, including the GPU-discovered partial-tile SSAO fix.

| Check | Result and scope |
| --- | --- |
| Native engine | Fresh Clang Debug, SDL3, assertions and warnings as errors passed. Both narrow fixes rebuilt successfully. |
| Anisotropy | Actual texture-manager upload regions include rectangular mip tails through 1x1. Native Vulkan samplers and descriptors pass GPU readbacks at 2/4/8/16, with off/on image contrast witnesses. |
| SSAO shaders | All eight mip wrappers compile and pass SPIR-V validation. Shared/subgroup and FP32/FP16 each pass 16 cases and 64 dispatches: 256 total, including odd extents, exact mismatch tags and independent eye clears/readbacks. |
| Actual renderer | Post-fix desktop and private simulated Monado stereo each pass 12 quality/full-or-half/MSAA combinations through the native renderer and resource restart paths. Desktop captures show the stock start map. |
| Save | Native worker/QC tests pass quiet manual purpose, catalogue-on-success, busy retry, failed replacement preservation, and v5/v7 save/load/signon round trips. |
| Reliable signon | Native staging/sender tests pass blocked and zero-return transport boundaries, message-lifetime chunk admission, metadata headroom/retry, exact-fit terminal marker and permanent-limit rejection. Socket outcomes are controlled in this fixture. |
| Real remote gameplay | Separate native client/server runs pass ordinary UDP and the `udp://` transport, complete private signon and 120 predictive movement/firing commands. |
| Bonk models/input/codec | All 29 original held-model identities, split meshes, ready frames and edges pass; mutations reject. Post-turn head capture works across three movement modes; legacy profile framing and bounded payload tests pass. |
| Original Bonk gameplay | Loaded original QC and real sweeps pass three damage tiers, three floor-hop tiers, walls, grounded whiffs, head-facing air dashes, native cooldown, jitter rejection and first-outcome tier lock across two victims. |
| Bonk command ownership | Production encoding/decoding and queue drains advance hit/whiff samples exactly once; expired, revoked, malformed and duplicate data reject appropriately. Real callback owner invalidation rejects the tail while retaining committed damage and restoring owned VM temporaries. |

The save/signon receipts certify their unchanged owners from `78819be5` with
the test-world lifecycle fix applied. Bonk fixtures use the final production
Debug graph after `1f2c44ec`; shader qualification uses the exact shader blob
imported as `e2b0f1c6`. The final release archive binds all platform packages
to one full Git revision and hash rather than shipping the diagnostic build.

No driver or global VR service reset was performed. Original proprietary QC,
programs, models and PAKs remain local and are not included in source or runtime
packages. Simulated OpenXR is a real GPU stereo software check; physical Steam
Frame appearance, live eye tracking and swing comfort remain unverified.
These are correctness checks, not performance measurements.
