# F05 real alpha composition and either-eye visibility

2026-10-01. Continue the frozen F05 owner after92656503. The current water
scene has zero alpha entities; mask/stage calls do not prove composition. Reuse
the native alias/model/static parser, efrags, alpha sort/draw, multiview water
passes and acquired GPU image/mirror. No new production renderer or feature.

Main creates two small deterministic paletted alias-model inputs in a disposable
licensed-stock profile. MDL format follows native modelgen.h; this is test
geometry, no committed asset bytes or mod-specific gameplay. Native late model
precache and Fitz static baseline parsing create two half-transparent models,
one wet and one dry, at separate screen regions near the existing actual e1m1
water plane. Native bbox-center leaf queries, PVS/efrags, alpha lists and GPU
draws determine admission; never assign visedicts, category caches or masks.

Luna/xhigh may write only tests/stereo_alpha_native_fixture.c, including the
entire cl_parse.c owner and a small main-callable scene helper. Encode native
MSG_Write* precache/static inputs and call actual CL_ParsePrecache/CL_ParseStatic;
preserve net_message/read cursor/error state. Use existing model/allocator/
texture/efrag owners. Two fixtures use first free model slots and native static
allocation; retained indices let a small phase helper set only alpha input
fields consistently. No rendering/state-machine/capture abstraction or copied
parser. Main calls after CPU submission task retirement, never joins from GDB.
Assertions enforce current loaded stock world, main-thread use, real models,
expected new static identities and no parse overflow. Bound helper~130lines.
No production edits/build/tests/branch operations/commits from worker. Main
owns docs, private asset/probe creation, compile/relink and GPU analysis; no
overlapping write scope. Do not revert concurrent work. Return scope/files/
checks/assumptions/open risks/followup <=400words; report missing evidence.

Actual native GPU output has four controlled layers per eye: background B,
entity-only E, water-only W, combined C. Freeze native simulation/time normally;
gamma/contrast1 and waterwarp0 remove unrelated varying effects in this narrow
composition oracle, retaining native MSAA4/SSAO1. For interior pixels affected
by both layers, compare C against the two distinct alpha-composition equations
in the native color/blend domain. Both-eye wet/dry assignment and reversed
arrangement must match expected native pre/post-water policy. Empty/occluded/
non-overlap geometry does not pass; inspect actual images and native list/draw
provenance. Reuse existing image capture and no microphone/focus/key input.

Either-eye-only bounds use independent projection plus native culling, with
actual output admission where possible; no gaze-driven visibility. Keep any
unproven boundary open. Retain diagnostic failures and natural exit/validation
checks; no benchmark/headset or exhaustive content matrix. New production
findings require a minimal source plan and appropriate local Astra/xhigh review
before implementation. Existing canonical tracking and desktop paths remain.

Before worker completion, main checked native r_sprite.c: entity alpha is not
applied to sprite vertices. Do not add a new sprite rendering feature solely
for this probe. Use the established native alias alpha path and small v6 MDL
inputs instead, retaining actual model/PVS/static/alpha ownership. Worker paths
become progs/vr_alpha_wet.mdl and progs/vr_alpha_dry.mdl, type mod_alias.

Main integration refinement before execution: add two ordinary private native
commands for initialization/opacity using the same helper functions. GDB only
registers them after native CPU submission retirement and queues text; model/
texture loading executes through normal engine command scheduling, avoiding a
blocking renderer/driver inferior call. Bound fixture becomes~155lines instead
of130; no second queue/policy or production command is introduced. This small
adapter reuses Cmd_AddCommand/Cbuf_AddText and remains inside the test module.
