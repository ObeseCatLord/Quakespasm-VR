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


2026-10-02 resumed geometry preparation: keep the existing parser/static helper
and two known wet/dry origins. Reconstruct only its lost private test inputs
with a small reusable Python emitter matching native modelgen.h/gl_model.c.
Two v6 MDLs have a single8x8 uniform fullbright paletted skin (wet250 red,
dry244 blue), four vertices in a12x1 vertical XZ plane at local Y0, four
triangles with paired opposite windings, one bounded frame and no flags/seams.
Native alias backface culling selects one pair, preventing winding ambiguity.
Fixed scale1/16, origin(-6,0,-0.5), byte vertices(0,0,0),(192,0,0),
(192,0,16),(0,0,16); normal index0, bbox from these actual vertices.
Header/skin/UV/triangle/frame bytes follow existing structures; expected308bytes.
The licensed stock palette is checked locally:250=(215,0,0),244=(127,191,255).
No asset bytes or user game/config changes are committed.

Luna/xhigh coding owns only new tests/prepare_stereo_alpha_native.py. Exact
output names remain progs/vr_alpha_wet.mdl and progs/vr_alpha_dry.mdl under a
caller-supplied **empty** output directory. Emit assets.json with dimensions,
geometry/palette indices/file hashes for retained setup provenance; refuse an
existing output/model path instead of overwriting. Do not run a game/GPU, edit
production/other fixtures/docs or commit. Main integrates at the existing native
parser helper, owns actual geometry/visibility/composition analysis and retains
all prior failed runs. Generator correctness/actual visible output must be
observed; emitted bytes alone are not F05 acceptance.

2026-10-02 skin preparation correction, before implementation: image B/E
comparison shows the authored strips do draw, but darken the background instead
of showing their intended colors. Native gl_model.c:4168 treats the top-left
skin color as connected background; a completely uniform indexed skin is
flood-filled to black before fullbright detection. Keep this upstream behavior
unchanged. Give the generated skin a single top-left index255 guard, which the
native flood-fill explicitly skips, and map UVs to the interior1..6 coordinates.
The other63 texels retain the intended fullbright color. Record the guard and
nonuniform skin accurately in assets.json. Preserve308-byte geometry/paths and
use a fresh private output directory; retain the first black-strip captures.
Require visibly colored native output before evaluating composition.
