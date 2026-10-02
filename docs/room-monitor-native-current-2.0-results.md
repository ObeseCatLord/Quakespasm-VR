# Native room / wet-only monitor results

2026-10-02. Frozen F08 component, local Astra/xhigh initial and correction reviews
complete. [Before-code plan](room-monitor-native-final-2.0-plan.md),
[verified senior brief](room-monitor-native-final-2.0-review-brief.md).
Production fix f814a05d adds only an underwater filter-history clear and comment
in native SA_ResetSelf, preserving existing callback exclusion and room/voice owners.

Luna/xhigh wrote the original optional394-line header. Main reviewed/integrated,
corrected native loaded-model/settings publication, then strengthened phase/PCM
oracles following senior findings. Actual native voice producer/capture policy,
Spatial/world exporter, SDK self queue/renderer and room effects/worker are reused.
Existing capture-and-forward renderer observer only; no private IR/room access,
synthetic mesh, duplicate loader/DSP/protocol/state machine or output-muting gate.

| Observation | Accepted software result / limit |
| --- | --- |
| Independent monitor permission | Prepared disconnected desktop/TX-off context. Native setters/forced refresh/Voice_Frame open dummy capture for explicit self permission; actual dummy self queue observed, no outgoing packets/sending. Not physical microphone/device qualification. |
| Wet-only / bounded self queue | Actual producer two20ms frames consumed with exact float-zero output when no room. Third frame exceeds1920 bound and increments drops. Public gain1 after reset is a prepared DSP control; native policy/default volume is a separate observation. |
| Actual BSP / worker / modes | Native loaded e1m1 server model sv.models[1], prepared client world/listener globals, native exporter13911 triangles/positive bytes. Worker completes and publishes sanitized finite-positive RT60. Parametric1/hybrid2 consume self PCM with positive combined wet output; no raw SDK estimate validity or unique convolution contribution claim. |
| Partial block / explicit reset | Public SA clock delta from reset boundary establishes first-half phase. Actual converted S16 output/last stereo-frame magnitude>=8, queued PCM, and positive continuation control are observed. Separately prepared live state then native reset clears queue and gives converted S16-zero output for16 blocks/4096frames/about85ms. |
| Live permission off | Another proven live first-half/queued/filtered state; native Voice_SetSelfReverb(false) invoked directly, without intervening explicit reset. Capture closes/queue empties; all4096 converted S16 frames are zero. Native producer subsequently admits no self PCM/network. This does not claim float-zero/unbounded-tail/device-buffer silence. |
| Replacement / clear / shutdown | Previously observed own-process room-acoustics IDs absent from subsequent named-task set; clear and attached shutdown restore baseline named set. Native joins/detach/context order source-verified. Not actual TID disappearance, exhaustive thread/race/leak or callback-contention proof. |
| Restoration / integration | Prepared core/profile/cvar/listener/world inputs restored; known dry coefficient1 restored and native capture refreshed. Previous11 checks and following pending voice/fatal/client/server/Host shutdown pass. Not every voice runtime state or live device cadence restored/qualified. |

Final private profile: /home/obesecatlord/FastGames/qsvr-room-native-_1heinas.
Build/run/input-stability/completion all0,12 markers, normal native shutdown.
377 dependency/link/runtime inputs incl221objects/1PCH stable through execution;
28 selected source hashes independently match. Exe SHA256:
9650d7780c0fec269601613d84c51ad902a8cb3ebed95af2c92bc61e0cc4a455.
Affected complete native graph compiles/links with the fixed SA owner. Snapshots
begin after fixture compilation, source selection after run; not exhaustive
compile-time/loaded-library attestation. Existing SDK4.8.1 and seven music inputs.

Failures and the earlier reviewed pass are preserved. First fixture compilation
used nonexistent sv.worldmodel; actual native owner is sv.models[1]. Initial worker
wait omitted native Spatial_Update after attachment; adding actual publication
matched normal frame behavior. Senior inspection found real missing downstream
filter-history reset. Stronger PCM regression compiles retained old SA owner and
links it with the exact same current fixture object/common graph; only owner and
executable destination differ. It aborts at explicit-reset PCM silence. Current
fixed owner passes all12 components. Negative run stops before permission off;
it proves explicit-reset sensitivity, while fixed-path permission-off passes.

Tiny post-revocation float output was independently measured (sum2.584e-8,
last3.045e-10), origin unknown. The native SDL3 callback clamps float and casts
scale32767 to S16. Fixture observes that actual format boundary without running
hardware callback; no arbitrary epsilon or production output gate. It requires
substantial converted live signal before silence, not merely float-positive noise.

## Senior dispositions

Both local gpt-6-astra/xhigh contexts verified; reviewers closed. Main checked
load-bearing source/receipts independently; second review also inspected both
executable disassemblies and confirms the added zero store.

| Finding | Disposition |
| --- | --- |
| Underwater accumulator survives reset | Adopted two-line native owner correction; old-owner PCM negative/fixed positive evidence verifies meaningful behavior. No adjacent mixer rewrite. |
| Partial phase / permission-off proof weak | Adopted actual clock phase, queued PCM, nonzero converted last frame and continuation controls. Public revoke tested directly from live state; no masking explicit reset. |
| RT60 sanitized / hybrid combines effects | Adopted bounded wording. Worker completion/safe publication/combined wet output only, no new production observability API. |
| Named task scan is not TID disappearance | Adapted narrower membership statement; source joins remain separate. No exhaustive thread claim. |
| S16 silence vs float-zero | Accepted native format oracle. Raw float residual origin unknown. Prepared hybrid/alpha.5/gain1/disconnected/TX-off4096frame window only; no callback contention/device-buffer/unbounded tail claim. |
| Regression attribution | Adopted. Negative proves explicit reset only; fixed-path public revoke independently passes. |
| Existing SDK/geometry ownership | Accepted native architecture.14 official tagged v4.8.1 sources/URLs/hashes verify managed TripleBuffer IR handoff and mesh/material copies. Not binary compilation identity/race proof; no IR deep copy/new loader/scheduler. |

All runs dedicated/noudp/nosound/nosteamapi/dummy/private preferences/licensed pak
reads; no GPU/OpenXR/window/physical audio or system settings. Frozen F08 device
retry/live callback/music timing and other F01–F08/F10 boundaries remain open.
Prior3204 ARM/Windows shipping reconciliation is historical after production fix;
final source/artifacts must be refreshed. This is not final goal acceptance.
