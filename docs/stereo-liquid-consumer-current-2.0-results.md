# Optimized native alpha consumer qualification

2026-10-02. No renderer change. Main verifies the missing helper observation is
an optimized debugger-location gap: stage1 jumps from task+135 to+1056 and rejoins
+171, bypassing inline helper+141. Named helper breakpoints cannot cover every
executed path. [Before-code observer plan](stereo-liquid-observer-2.0-plan.md).

Published probe instead observes the actual non-inlined R_DrawEntitiesOnList
consumer. Native chain/context identifies pre/post-water stage; each call must
use the exact eye (descriptor,dynamic offset) pair and serial alpha list mode.
The uniform ring intentionally shares a page descriptor across eyes; distinct
live offsets select distinct eye blocks. An initial descriptor-only uniqueness
assumption fails and is retained; source R_UniformAllocate establishes the correct
pair identity. No expectation is weakened and no eye selector/list is assigned.

All5 phases now pass at the unchanged shipping0bd4ddb1 x86-64 package. Actual
water/empty and reversed eye leaves, runtime head valid1/tracked0, canonical
basis0, categories1/masks1 and2/exceptional1, all4 expected stage/pass/eye consumer
combinations, and native MSAA4/SSAO1/OIT0 pass. Ordinary OIT1 retires category/mask/
exceptional state and recorded consumer calls, preserving quality. GDB/client
exit0, normal inferior exit, explicit pass marker and passed result-reset.json.
Main inspects all4 opposite-eye native mirrors: water plane and nearby room/stone
geometry are rendered with opposite eye placement and both roll arrangements.

Current package engine hash remains
1635eee05109b4e0abf48fc0a47d2a1ed20098b1892d5d605e975f032c7a1783.
Private root FastGames/qsvr-liquid-consumer-whynjsa_: liquid.gdb/log/entry/exit,
liquid-output/result-reset.json and5mirror PNGs, first-consumer failure retained.
Existing isolated simulated null Monado, native package Vulkan images/mirror and
licensed stock assets are reused. No physical tracking/input, gaze/foveation or
performance measurement. No validation layer is enabled; do not infer validation
cleanliness. No NVIDIA reset/reload/user input/unrelated runtime changes.

This closes the renewed optimized-package opposite-liquid selector/state/output
boundary. Both alpha-entity lists are0 in this stock scene, so actual translucent
entity/water composition and either-eye-only authored bounds remain open F05.
No whole renderer/group/integration or headset acceptance follows. Earlier failed
helper result stays in [resumed GPU receipts](gpu-resumed-current-2.0-results.md).
