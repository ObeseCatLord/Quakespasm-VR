# F06 actual-backend transition qualification

2026-10-01. Reuse `vr_openxr_vulkan_fixture.cpp` as the existing backend fixture
translation unit (rename its main), and call production
`VRXR_UpdateVulkanFoveation`. Prepare the current session/frame/acquired-waited
boundary; replace only external XR dispatch with controlled profile updates and
META center query. Record ordered profile calls and query counts. No replacement
runtime, public API, renderer rewrite, provider or hardware pass is implied.

Required cases: off/default and unavailable eye stay off; explicit fixed only;
three-sample eye stability and fresh eye updates; opt-out/focus/invalid flags,
NaN/out-of-range second center clear both outputs/reset/rearm; partial second
swapchain setter failure restores both off; failed restoration returns error;
array versus two-chain ownership; inactive/copied frame rejects updates. Reuse
existing stale-gaze policy/rate-map checks rather than copying their rules.
If an assertion reveals a production defect, report evidence first; no fixture
assertion weakening and no production edits under the worker contract.

Ownership: worker only `tests/openxr_foveation_fixture.cpp`; main owns this plan,
README/receipts/checklist and integration. Strict existing C++14/SDL3 warning
recipe, then execute fixture. Output must distinguish prepared dispatch proof
from real borrowed images, selection, KHR GPU protection and user-deferred
FB/META runtime/gaze/device behavior. F06 stays open for its other boundaries.
