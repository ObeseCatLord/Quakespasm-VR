# Q01 per-eye liquid-category ordering plan

2026-10-01. Before-code plan following the
[Astra xhigh disposition](stereo-water-transparency-2.0-review.md).
Two views only. Native desktop/OIT, shared sorting, world-water drawing,
conservative visibility and opaque single-pass stereo remain existing owners.

## Verified incompatibility and smallest adapter

gl_rmain.c's alpha stage uses the center leaf for both eyes. Native vkQuake4bc898
expects the camera's liquid category to select entities before/after world water.
Primary51b deliberately shares its sort origin, so sorting once is retained.
For wet-eye bits E, the existing across-water stage draws overwater list with E
and underwater list with 3^E; the matching stage reverses those masks. Zero skips
a list, three draws once for both views. Agreement can differ from center.

Use the existing 160-byte scene payload: eye_offset[].w is currently zero and
shader offset consumers use xyz. A value one excludes that view; zero participates.
The common shader macro assigns finite outside-x vec4(2,0,0,1) for exclusion,
otherwise the existing clip correction. Keep it one assignment for the unbraced
basic.vert caller. No new uniform layout, fragment policy, material push constants,
render passes, independent sort or renderer. Official-contract inference and
rendered-proof limits are recorded in the linked review.

## Frame, recording and transform stages

1. In existing R_PrepareStereoFrame, clear category eligibility/mask/exceptional
   handles at entry. Query the eye origins only for a valid rendered stereo world
   frame with finite eye positions and a valid tracking basis; worldless/fallback
   frames retain native behavior. Publish category state with the existing frame
   state. When E=1 or2 and native alpha sorting is enabled, copy the effective
   scene payload (including waterwarp) into two R_UniformAllocate allocations,
   left-only/right-only, preserving each descriptor and dynamic offset. Do not
   allocate on agreement, OIT or sort-off. No pose/gameplay updates here.
2. Add a nullable scene descriptor override plus offset to cb_context_t. Native
   R_BindPipeline chooses it only for scene draws; UI/display routing stays native.
   Clear it at primary/secondary context reset in gl_vidsdl.c, at context setup,
   and after each exceptional list. No successful-path-only lifetime assumption.
3. Preserve the existing indexed alpha task/DAG. Check the published category
   state inside the callback after dependencies finish. On eye disagreement,
   invocation zero records both existing stage contexts sequentially, invocation
   one returns. Otherwise tasks retain separate stage ownership. This prevents
   new concurrent duplicate alias-lightcache/model/brush-tree writes. The native
   command context order still executes across-alpha, world water, same-alpha.
4. Select each list's mask, skip zero, draw with ordinary or exceptional scene
   payload through R_DrawEntitiesOnList, flush/end the native list work before
   changing/restoring the selector. Native OIT/composite/fallback paths remain.
5. Replace local nonalias pitch mutation with a shared observational angle helper
   returning raw entity angles and the intended one-time local-player 0.3 pitch.
   Use it in existing brush draw/showtris transforms and sprite oriented/angled
   consumers. Remove both gl_rmain.c mutation sites. Preserve entity pointers for
   indirect brush instance claims: a shallow entity copy would change those
   identities, so do not use one. Alias interpolation already handles its own
   local pitch and remains unchanged. Brush showtris uses a local negated pitch
   rather than temporary shared entity mutation. No entity-preparation service.

Exact write set: Quake/gl_rmain.c, Quake/glquake.h, Quake/gl_vidsdl.c,
Quake/r_brush.c, Quake/r_sprite.c and Shaders/stereo.inc. Native R_UniformAllocate
needs no edit. One Luna xhigh worker owns all six tightly coupled files; main
reviews/integrates, other workers are disjoint. Plans/docs remain main-owned.
The observational helper may live in gl_rmain.c with a glquake.h declaration.
Brush bounds/indirect admission test angle presence, so removing mutation does
not require a new culling owner or global interpolation policy.

## Scale, failure path and eventual proof

Expected patch: roughly 150–220 net production lines, primarily existing frame/
callback seams and local angle reads. This refines the brief's unverified
100–160-line estimate to include the confirmed reset and transform seams, without
new topology/state owners. Stop and reopen if new pass variants, broad entity
preparation, another policy owner or additional production modules are needed.
Do not silently change desktop material/render settings to make the adapter work.

After all implementation finishes, qualify actual two-layer rendered category
compositing for both disagreement directions, both agreement states including
center mismatch, transition/worldless fallback, tasks on/off, repeated local
pitch, brush liquid surfaces and MDL/MD3/MD5/sprite coverage. Include sort/OIT
toggles, MSAA/AO, full-rate/FB-META/KHR allowed foveation modes, ordinary world/UI,
and once-per-frame gameplay/pose preparation. Compile every participating shader
variant at that final phase. Source inspection is not rendered acceptance or a
performance measurement. No tests/builds/probes/fixtures/game runs during coding.
