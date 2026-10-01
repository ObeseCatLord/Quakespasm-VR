# F06 real-GPU KHR submission subset

2026-10-01. Existing host binary through482cd9f5, isolated Monado simulated
service, private SDL window and writable profile/read-only pak0. GPU supports
KHR; current simulated runtime exposes no actual gaze/FB/META provider. Reuse
native commands, frame/resource/render owners, screenshot and normal quit.

Explicit fixed request must select KHR, produce both full/coarse rate-map tiles,
upload identical map bytes, complete loaded stereo frames and eligible/protected
world dispatch. Menu context must make the retained map entirely full rate.
Request eye mode with unavailable provider must disable foveation, never remain
fixed; ordinary off must continue loaded stereo. Require native4xMSAA retained,
no production assignment of capabilities or gaze. Inspect fixed/menu screenshots,
Vulkan validation/synchronization output and normal exit. Software backend
fixtures separately exercise META centers/failures; hardware gaze remains deferred.

Main owns only private scratch script/profile and this receipt/plan. No input
focus, mouse/keyboard events, system mic or user runtime/profile changes. Failure
is evidence to investigate, not a pass or excuse to switch provider/renderer.
This subset does not establish eye-driven real GPU patterns, provider metadata,
whole-map output, latency or speed. Follow existing lifetime and feature-family
selection; no new compositor, live family switch or device recreation.
