# Reopened movement reuse and mod-special-case decision

Status: architecture reopened following the user's direction to avoid a growing
collection of mod special cases. No new production change is authorized before
this comparison receives local Astra review and a concrete disposition/write set.
Keep the full migration goal: QSS-M-style modern predictive desktop/VR crossplay,
QC/mod abilities, inherited VR contacts/roomscale and deliberate QBJ3 ladder fix.
Working boots/AD pickup behavior is reference evidence, not permission to write
a new ability implementation. Main/code only on2.0; user-dirty migration doc stays
untouched. Hardware/performance/Windows/ARM checks remain deferred.

## Verified evidence and unknowns

| Claim | Evidence / significance |
| --- | --- |
| [verified: installed pack/program bytes] AD pak2 and q30 program are identical. | SHA2565e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340,2347206 bytes. Actual game ad normal session and boots modes pass existing exact-program assertion and software vertical. No AD-specific implementation is necessary for these bytes. |
| [verified: current production]2.0 admission pins stock or exact q30; native-state checks name q30 fields/ability bits/scheduled attack roots. | sv_main.c:862–915,1648–1684; sv_phys.c:7680–7740,8112 and private walk/native boundaries. It preserves tested QC behavior but makes broader mod prediction depend on repeated qualification and exceptions. This must not silently become the final generic compatibility architecture. |
| [verified: QSS-M source] QSS-M can choose per-command PMove without a program whitelist. | QSS-M/Quake/sv_user.c:635–706 chooses usingpmove from SV_RunClientCommand or sv_nqplayerphysics; PreThink, Think, cooperative hook or PF_sv_pmove, PostThink. Its receipt dispatch is not portable into our existing world queue without duplicating execution. |
| [verified: QSS-M source] QSS-M does not promise authored unaware-mod forces in that PMove branch. | sv_nqplayerphysics default1 at sv_user.c:49 keeps unaware mods native. Its explicit independent branch resets all PreThink velocity to saved velocity when no cooperative hook exists (675–686); generic jumpspeed270 in pr_ext.c:2081. This is evidence to compare, not proof that native boots work under that independent branch. |
| [verified: product source] Inherited/current Quakespasm VR has a shared generic command owner with a legacy wrapper. | quakespasm-openvr/Quake/sv_phys.c:5939–6145 calls unaware PreThink once per world frame, then consumes queued PMove commands; cooperative hooks run per command. Its filter at5665–5742 subtracts expected stock QC drag/jump from a velocity delta, preserving residual authored forces. It intentionally stops for SV_QBJ3NeedsLegacyPhysics. Its sv_nqplayerphysics default0/preserve_qc_velocity default1 are in sv_user.c:56–61. |
| [verified: prior source reviews / current API]2.0 already has private queue/completion/epochs, shared QSS-M solver/stats/replay, fresh native frame, VR input/contact and QC input ABI. | Existing movement/mod plans, sv_user.c/private input owners, sv_phys.c/private walk, pmove.c/h, cl_main.c and PR_GetSetInputs. Preserve these working state/transport owners; absence of a cooperative invocation does not justify a second queue. |
| [verified: focused runtime] Actual spawned/contact/timed boots work under current native ability dispatch. | tests/stock_liquid_native_fixture.c -q30boots actual admission/QC/action/expiry. Native/selected VR and desktop runs match112 zero-axis jump samples at printed1e-6; actual AD run passes. The new work adds a fixture, not production boots logic. |
| [unknown] Which generic wrapper best preserves actual unaware-QC gameplay and modern replay? | Neither 'unverified' nor a hash whitelist proves replacement necessary. Compare actual boots and ordinary commands against existing native reference with existing fixtures; avoid building an ever larger branch-proof/callback scheduler before a usable shared path. |
| [unknown] Native prediction/correction limits acceptable under arbitrary QC forces. | Client lacks arbitrary server QC. Preserve gameplay and use existing correction/authority metadata; do not infer a duplicate ability state machine from final velocity or claim exact arbitrary-force replay. Ordinary prediction and usable supported ability behavior remain required. |

## Mostly-worked design comparison for Astra

Right-size: solo operator, retain working transport/server/renderer and existing
QC lifetime. Prefer one shared movement integration with a narrow VR adapter and
QB J3 ladder compatibility, rather than per-mod implementations. The existing
exact-q30 path can remain a known reference during convergence; do not delete it
blindly or append more hashes/ability gates as the default expansion strategy.

1. **Adapt the inherited generic wrapper at existing2.0 owners (current lean).**
   Reuse once-world unaware QC versus command-local cooperative ABI, shared PMove,
   current accepted queue/input/contact/completion and published epochs. Do not
   copy whole old sv_phys.c or weaker network port. Verify the old residual-force
   reconciliation rather than assume it is correct; when it fails, isolate that
   compatibility gap at QC/solver boundary. Expected first vertical: normal
   private/public session plus actual boots in a second non-whitelisted program,
   from existing driver, before broad map/ability expansion. Need identify one
   specific generic wrapper implementation and exact replay/correction behavior.
2. **Port QSS-M independent QC/builtin semantics exactly into world dispatch.**
   Reuse PR_GetSetInputs, registry, physent/solver; preserve world queue instead
   of receipt-time execution. But unaware PreThink force reset could erase boots
   or grapple and cannot be accepted merely because QSS-M source has the branch.
   Its default native mode alone does not finish modern predictive gameplay.
3. **Keep exact-program ordinary classification plus new gates/hashes.**
   Existing behavior is qualified and reusable, but this path risks making
   callback/ability audits and per-program lifetime policy the entire migration.
   Retain as bounded reference only if a shared adapter demonstrates inability;
   require a deletion/convergence plan if it remains temporary scaffolding.

Rejected without evidence: new per-mod ability solvers, another native/queued
physics service/protocol, whole client/server rewrite, generic force arithmetic
accepted without native comparison, or blanket PMove that deletes authored QC.
Do not generalize valid defensive packet/state checks into a demand to whitelist
all mod bytecode. Program-specific VR weapon/aim/melee bridges with demonstrated
QC incompatibilities are a separate requirement; this review must not delete
those on the strength of a movement preference.

Open decisions for local Astra: which existing wrapper should become the shared
owner, what can be deleted/folded into it, which specific incompatibility requires
an adapter, smallest end-to-end next proof and precise production write set.
Rank by behavioral fidelity and maintenance. Merge repeated ownership questions;
do not re-review OpenXR/Vulkan/graphics or all q30 scheduling closure. No human
approval/taste decision is expected; the user has supplied the reuse preference.
Main must spot-check findings and synthesize adopted/adapted/rejected disposition
before committing the implementation contract. Stop expansion if a second
scheduler/state machine or repeated interactions exceed this focused boundary.
