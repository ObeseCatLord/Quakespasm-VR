# Native mod entity consumer repairs

2026-09-30. Before-code plan; baseline `e65f727e`. Bounded MOD-008 source
reconciliation, preserving vkQuake rendering rather than copying another
renderer. Linux/ARM software qualification follows all implementation.

## Verified behavior and incompatibilities

Requested local Astra xhigh examined actual entity-alpha, effects, static and
CSQC consumers against primary51b452c and donor4bc898f. Main verified the two
load-bearing findings in current code and the corresponding native owners:

- `pr_cmds.c:PF_cl_makestatic` builds current QC fields into `stat->baseline`,
  copies that state into `stat->netstate`, then mistakenly copies the source
  edict's older `ent->baseline.origin/angles` into render state. A newly
  allocated edict starts with `nullentitystate` (`pr_edict.c:ED_Alloc`). Native
  `cl_parse.c:CL_ParseStatic` instead uses the newly decoded static baseline.
  This is a donor defect, not a missing allocator or a second static entity API.
- `sv_main.c:SV_BuildEntityState` already carries optional `.modelflags` in the
  effects upper byte. `cl_main.c:CL_RelinkEntities` merges them with the model
  header into `modelflags`, but only rotation tests that merged result. Its
  seven fallback trail conditions still test the model header alone. Thus a
  requested rocket trail on an otherwise unflagged model loses its trail/light.
  Explicit entity and model scripted trails already precede those conditions.
- Alpha decoders, alias and brush/liquid consumers, native light effects and
  network-static efrags/draw submission are source-present. Primary wire
  `drawflags` storage has no demonstrated renderer consumer at these pins;
  unsupported primary `RF_*` particle branches do not require another renderer.

## Minimal adapter versus replacement

Use the already-built static baseline for both transform copies, before native
efrag linking and edict retirement. Use the existing merged `modelflags` in the
seven fallback GIB/ZOMGIB/TRACER/TRACER2/ROCKET/GRENADE/TRACER3 tests. This retains
their order, pause behavior, scripted precedence, particle owner, rocket-light
allocation, packet layout and model-header flags. No new flags, protocol,
entity cache, static list, particle renderer or mod-name special case is needed.

Replacing QC state construction or the renderer would duplicate working
ownership without addressing a wider demonstrated incompatibility. Native
OIT/materials/rerelease masks, ordinary entity alpha and all unrelated effects
remain owned by their existing paths. Expected write set: `Quake/pr_cmds.c`
and `Quake/cl_main.c`; exactly two transform-source and seven condition edits.
No interface/signature/registration, loader, server or pipeline change.

## Before-code source advice disposition

| Finding/recommendation | Disposition |
| --- | --- |
| P2: newly created CSQC statics use an unrelated edict baseline. | Adopt. Two transform copies use the baseline already built on the static. Preserve allocation/fields/linking/free order. |
| P2: merged entity model flags are ignored by fallback trails/light. | Adopt. Change only the seven fallback tests; scripted trail precedence and default model flags remain. |
| Import addentity/renderflags or unsupported primary particle drawing. | Reject unsupported scope inference. No implemented inherited consumer was demonstrated at the pinned references. |

The requested local Astra review established bounded source findings, not
effective-model certification or runtime behavior. Main verified their evidence.
No design fork requires user input; reopen if implementation needs other files
or duplicates an owner. Delegate one worker for the two tightly bounded edits;
main reviews its full diff, then request bounded final source review.

## Final software qualification

After all implementation, invoke actual CSQC spawn/setorigin/setangles/setmodel/
makestatic through the native VM, then efrag collection and rendering in desktop
and stereo. A static must retain authored nonzero translation/rotation, skin,
frame/alpha/effects and the native edict retirement; repeat across map reload.
Helper-only copies are insufficient.

Exercise actual server field-to-snapshot-to-relink trail consumers with entity
flags on unflagged models, model-only flags, both sources, all seven precedence
branches, zero flags, pause, teleport/reset, explicit scripted entity/model
trails and rocket light. Preserve native alpha/OIT and ordinary effects.
No execution, compiler, build, test or benchmark belongs to this source slice.
Rendered/software evidence remains pending; user hardware/performance trials
and Windows builds remain deferred.

## Source integration receipt

Implemented `05e881cc` after before-code plan/disposition `22ceceb6`: exactly
the nine planned replacements in the two owned production files. Main reviewed
the complete delegated diff and surrounding state, linking, flags and trail
ordering. Scoped diff hygiene passes. The final requested local Astra xhigh
review found no P1/P2 blocker in the actual patch: static transforms come from
the newly built baseline, and all seven fallback tests consume merged flags
while scripted precedence and native light/particle behavior remain.

No entity/protocol/rendering owner or interface was introduced. Effective
reviewer settings remain uncertified. Actual rendered/static/trail/light and
broader MOD-008 software qualification are pending; no execution validation ran.
