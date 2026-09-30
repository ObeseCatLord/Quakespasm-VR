# Inherited recipient visibility and attachment snapshot adapters (NET-003)

Date:2026-09-30. Plan before production; `2.0` only. Await coherent NET-009
source acceptance before editing its shared snapshot region. This is a separate
remaining migration requirement, not proof that custom transport is complete.

## Evidence and behavioral references

`docs/migration-network-map.md:NET-003` requires replacement visibility and
customization, not just baseline delta flags. Direct reads show:

* Primary master51b452c0 and QSS-M03a498aa register
  `customizeentityforclient`, `viewmodelforclient`, `exteriormodeltoclient`, and
  `pvsflags`; destination `progs.h` has none of these. Existing native
  `nodrawtoclient`/`drawonlytoclient` stay reusable.
* Their snapshot builders call customization with self=entity and other=recipient,
  follow attachment `tag_entity` parents for PVS, emit view/exterior flags, and
  distinguish ordinary/ignore/PHS PVS modes. Primary bounds parent depth; QSS-M's
  unbounded walk should not be copied. Both define PVSF_NOREMOVE; primary's current
  snapshot path checks it on invisibility, though broader no-remove behavior
  still requires reference qualification.
* Destination snapshot sets `parent=ent` without following existing tag fields.
  Native entity state packing already transmits tag entity/index, and client
  `CL_AttachEntity` already applies parent transforms and inherits
  VIEWMODEL/EXTERIORMODEL flags. Its tag_index application remains a TODO and
  chain depth is limited to ten. No new attachment transform, renderer, entity
  service or culling graph is needed in this slice.
* Primary admits emitting entities without a model. Native server state packing
  and client scripted particle emission already exist, but native model-null
  early rejection makes the smallest correct end-to-end adaptation unresolved.
  Merely removing the server model test cannot qualify model-less effects.
* NET-009 now owns current custom eligibility, retained removal ACK boundaries,
  mandatory native recipient player snapshots and shared packet logs. New
  filtering must use those owners; no copied primary pending-array policy.

These are source facts, not actual mod/session/visibility results. No tests,
builds, runtime probes or performance measurements have run for this slice.

## Minimal design versus alternatives

Copy the four optional field declarations and bounded primary/QSS-M recipient
checks at existing native snapshot points. Reuse native fat PVS, state builder,
client flags, tag transformation and particle system. Customization remains a
QC callback rather than a new C policy or per-mod branch. Preserve its reference
mutation semantics while borrowing/restoring native callback context and
retaining live edicts across callbacks that may remove them. Decide explicitly
how customization may alter the recipient's mandatory native player presentation
without omitting its movement/collision seed.

Following a bounded native tag chain fixes the demonstrated parent-PVS gap;
validate current-VM, allocated/live references and cycles at that boundary.
Do not import an entire fork snapshot builder, duplicate filtering in the custom
writer, force a second PVS, invent a prediction authority or alter Vulkan tasks.
Native PVS remains default; skyroom expansion and broad always-send co-op player
policy are outside this slice. QSS-M's PHS mode conservatively bypasses PVS;
copying that advertised behavior does not mean implementing a new PHS service.

Expected scope under200 lines in `progs.h`, `server.h`, `sv_main.c` and only
if proven necessary the existing `cl_main.c` particle rejection boundary.
Reopen if a second visibility owner, scene path or protocol extension appears.

## Decisions requiring verified design disposition before code

1. Customization/recipient filters must preserve the required native owner seed.
   Lean: keep snapshot owner mandatory and separate its visible presentation
   decision at existing state fields, rather than skip the whole record. Verify
   against native prediction and inherited first-person/avatar behavior.
2. PVSF_NOREMOVE can retain an intentional custom mapping while invisible but must
   never preserve an actually freed/reused lifetime. Define scope using the
   reference behavior, existing CURRENT/REMOVEWAIT flags and explicit free hook;
   do not override removal ACK debt with a visibility hint.
3. Model-less emitter acceptance needs actual primary/native client particle
   ordering evidence. Adapt only the existing particle/state consumer if needed;
   preserve default vkQuake effects and model draw submission unchanged.
4. Recipient customization can mutate fields between recipients. Preserve
   reference semantics and the collected snapshot generation without wholesale
   rollback or another state cache; live/free checks must follow callbacks.

Main drafts a verified brief after the missing client-emitter evidence is read.
Local requested-Astra design advisory challenges necessity and deletion options;
record its disposition before production. No certified senior gate can be
claimed without effective settings evidence. Implementation may use one bounded
coding agent at this tightly coupled region, with main final integration.

## Final acceptance, after full implementation

Actual normal desktop/private VR/public peer session: customize two recipients
with different model/skin/filter results; attach a child stored near world origin
to a moving visible parent; normal/notrace/ignore/PHS modes; view/exterior flags;
no-model emission where qualified. Verify ordinary missing fields remain native,
invalid/freed/cyclic parent chains are bounded, customization freeing self/other
retains safe ownership, hidden custom state obeys no-remove without resurrecting
freed/reused slots, and local native movement seed survives each hide policy.
Check native desktop geometry/particles, VR two-eye culling and avatar/weapon
presentation. No mod-special-case engine rules. Performance measurement and
live headset tests remain user-deferred. Source predicates alone cannot qualify
user-observable scripted effects or full migration parity.

## Additional direct reference checks before design review

Actual primary `cl_main.c:CL_RelinkEntities` (model-null rejection near2221,
emission near2425) and pinned QSS-M (2136/2362) also skip model-less entities
before scripted emission. Destination does the same. Therefore no inherited
working model-less **rendering** behavior is established: preserve the native
client scene path in this slice. Server-only emitter state eligibility can
match reference transport, but must not be described as rendered particle parity.
Primary's bounded/named particle-index check is separately useful evidence;
any repair at that existing consumer requires its own demonstrated gap/plan.
The current getentity builtin likewise rejects entities lacking a model.

Classic clients use the separate native `SV_WriteEntitiesToClient`, which retains
its two-pass distance sorting and serialization; QSS-M has customization and
view/exterior suppression at its immediate serialization loop. Do not copy its
`EDICT_TO_PROG(client_t *)` calls: the recipient is the native client edict. A
shared callback-context/liveness adapter is reusable, but adding callbacks to
native gather/sort must not let previously collected entity slots be freed and
reused before serialization. Resolve this ownership in the verified brief before
editing that path, retaining native sort rather than transplanting QSS-M's writer.

The destination attachment client already inherits view/exterior flags and its
renderer consumes EXTERIORMODEL; that is not proof of complete VIEWMODEL behavior.
The existing flags can hide owner geometry without clearing model/collision
metadata, but actual first-person/chase/VR/avatar/particle semantics require
source review before choosing that presentation adapter. Do not assume setting
owner modelindex0 is harmless: existing client relink and getentity depend on a
model. No new behavior is implemented by this evidence checkpoint.

### Remaining ownership and presentation decisions

Further native source reads confirm `ED_Retain` prevents a freed slot from
entering the reusable list until its matching release (`pr_edict.c:188-202`),
but does not keep the edict live. The classic gather/sort path therefore needs
both retention and a post-callback/free check if customization runs during
gather. Retain every admitted candidate until serialization finishes, including
the recipient, release every candidate after an overflow break, and skip freed
candidates during serialization. This is a proposed use of the native lifetime
owner, not an adopted implementation: reviewer must compare it with callback
at serialization and reject needless storage or a duplicated state builder.

The destination renderer rejects EXTERIORMODEL in both opaque and transparent
entity passes (`gl_rmain.c:1198/2317`), while client relink performs lighting,
trails and emission before omitting the ordinary local player from its draw
list (`cl_main.c:2366`). Thus the flag hides ordinary geometry without erasing
the model or native prediction seed, but is not a general suppress-effects
policy. It also hides geometry in chase view. Existing co-op overlay paths
have their own eligibility checks, so merely setting this flag cannot qualify
all presentation consumers. Do not expand the renderer to fix this speculatively.
The design review must explicitly resolve hidden mandatory-owner semantics and
the scope of presentation flags before production.

No native VIEWMODEL camera transformation was established by the symbol reads;
the QSS-M reference additionally uses it for lighting sampling. Copying the
recipient flag preserves advertised metadata, but must not be described as a
complete camera-relative or tracked-hand rendering implementation. Keep actual
native/VR weapon, avatar and attachment transform owners; a demonstrated gap
would require its own bounded adapter plan rather than a fork renderer import.

## Mostly-worked verified design brief

Solo operator; additive optional mod fields at existing server writers. The
previous goal turn made production progress (`39ea8209` stable custom retirement)
and its bounded advisory correction is now source-accepted. Full migration and
final software/runtime qualification remain open. All facts below describe
direct source inspection; source acceptance is not execution evidence.

| Environment / reusable boundary | Evidence |
| --- | --- |
| Writable repository / user edits | `quakespasm-2.0`, branch2.0 previously checked; only user-owned `docs/migration-2.0.md` dirty before this brief. Do not edit/stage it. [verified: status] |
| Read-only references | `quakespasm-openvr` master51b452c0, `QSS-M`03a498aa, native baseline `vkquake`4bc898f2. [verified: pinned earlier source inventory] |
| Modern native writer | `sv_main.c:2521-2690` builds compact entity states immediately in entity order; owns custom CURRENT and retirement alongside ordinary deltas. Recipient stays native-only. [verified: direct read] |
| Classic native writer | `sv_main.c:3814-4148` gathers indices, sorts distance bins, then serializes live edicts. Existing arrays are uint16 indices, no entity cache or callback today. [verified: direct read] |
| Reference callback | Primary2494/QSS-M1344 set self=entity, other=recipient, execute before visibility, false hides, persistent field mutation. QSS-M classic uses an erroneous client_t pointer conversion; do not copy it. [verified: direct read] |
| Lifetime | Native Retain/Release prevents slot reuse, not ED_Free. Debug entity conversion rejects free edicts. [verified: pr_edict188-202/2370] |
| Presentation | Native EXTERIORMODEL hides ordinary opaque/transparent geometry; local relink effects occur earlier, co-op overlays have independent eligibility. VIEWMODEL is metadata here, not proven camera-relative transform. [verified: preceding client/renderer reads] |
| Required owner seed | Selected writer requires recipient native state; native packing includes movement velocity/type, solids and prediction metadata. Dropping that record is not an acceptable hide policy. [verified: state builder and selected writer reads] |
| Parent / PVS | Native attachment transforms exist; modern server comment does not implement parent walk. Primary bounds depth, QSS-M does not; both expand USEPHS to bypass PVS. [verified: reference loops] |
| No-model effects | Both reference clients and destination reject model-null before particles. No new rendering path justified. [verified: preceding evidence] |

### Proposed decisions (main lean, awaiting advisory)

1. **Shared callback/recipient check, native owner exception.** Copy optional
   fields and PVS constants. Use one local callback helper borrowing raw self /
   other, retaining self while QC executes, restoring both, returning false on
   rejection/free. Retain recipient across its writer pass; after each callback
   check recipient liveness before any conversion or later state construction.
   On an actually freed recipient, stop the client send through the existing
   recipient failure owner, rather than construct a fake seed. For live owner,
   execute customization and keep field mutations; ignore boolean/recipient
   *omission* for its mandatory state. Lean: add EXTERIORMODEL for a requested
   hide on modern owner, preserving native model/collision data and ordinary
   effects; classic has no exterior flag so keep its mandatory native record.
   This is an explicit transport limitation/owner exception, not complete hidden
   effects parity. Rejected: modelindex0, fake second entity, new wire visibility
   flag, arbitrary callback rollback or skipping all owner customization.
   [proposed; exact consumer scope to be challenged]
2. **Classic mutable callbacks and sorting.** Run callbacks in gather, then
   retain each admitted candidate until all serialization/overflow paths finish.
   Retain recipient first; check free before using fields; omit candidates freed
   by later callbacks, release even after packet overflow. Reuse existing sorted
   index list for cleanup; no new cache, full entity-state copy or sorting owner.
   Recipient flags may be read from current fields during serialization, as
   native classic writer already does; gathered callback/PVS decisions are not
   rerun. Native distance ordering necessarily differs from QSS-M immediate
   output, so promise its optional field semantics, not identical callback timing
   or immutable snapshots. Rejected: wholesale QSS-M writer replacement; callback
   at serialization after stale model/PVS selection; retaining only the current
   callback edict while later collected slots can be freed/reused.
   [proposed; may merge with decision1]
3. **No-remove only at live custom invisibility.** PVSF_NOREMOVE retains a live
   eligible SendEntity mapping during invisibility, clearing CURRENT and low
   dirty bits but preserving PRESENT/REMOVE/WAIT/RETIRENEW. Re-entering visible
   eligibility from CURRENT-clear forces a full callback update, so cleared
   hidden dirty changes are not lost. Explicit free/native transition always
   uses existing retirement helper; no-remove never cancels outstanding removal
   delivery debt or restores an old lifetime. Classic native lifetime has no
   custom mapping, so do not invent no-remove persistence there. Rejected:
   leaving CURRENT enabled outside PVS, removing pending ACK debt, another hidden
   entity list, or implementing visibility policy a second time in custom writer.
   [proposed; interactions with stable-boundary state must be verified]
4. **Parent PVS + flags.** Shared bounded parent lookup validates raw QC entity
   offset against current allocation/edict size before conversion and rejects
   free parents/cycles. Bound depth by num_edicts; do not require parent to be
   network-visible just to consult its leaves. Reuse each writer's existing
   leaf-count policy, including modern zero-leaf bypass and overflow conservatism.
   Modern viewmodel recipient match adds VIEWMODEL and bypasses PVS; mismatch
   hides. Classic cannot handle camera-relative VIEWMODEL, so suppress non-owner
   viewmodel entities like QSS-M. Modern exterior recipient match adds existing
   flag; classic suppresses non-owner exterior entities. NORMAL/NOTRACE retain
   native PVS; USEPHS/IGNORE bypass it as references do. No skyroom/co-op global
   expansion or new tracing service. [proposed; constants and field matches verified]
5. **Keep native particle path.** Do not extend client rendering for model-less
   entities. Native emission metadata eligibility may match reference server
   admission; preserve native alpha/model limiting and actual effects consumers.
   Bound/named emission-index consumer is a separate small demonstrated safety
   repair, not a reason to replace particle rendering. [proposed; source ordering verified]

Suspected overlap: decisions1/2 are one mutable-QC ownership boundary, and
decision3 must stay in NET-009's existing pending-state owner rather than become
a second visibility state machine. Merge/delete helpers or proposals if simpler.
Expected production under250 lines in progs.h/server.h/sv_main.c; the increase
from earlier200 is explicitly for classic retention and complete cleanup, not a
new service or renderer. Reopen if full snapshot caches, duplicated callbacks,
new packet semantics or repeated lifetime-layer repairs are needed.

Request one local Astra Max advisory: verify load-bearing claims, rank the real
forks, deeply specify the highest-risk ownership decision, identify missing
safety and deletion opportunities. Return prioritized recommendations and only
genuinely human decisions (if any), <=1400 words with file/line evidence. No
edits/builds/tests/compiler checks/probes/nested agents. Do not re-review whole
renderer, protocol negotiation, prediction physics, foveation or approved NET-009
state except direct no-remove/callback interaction. Effective settings metadata
must be exposed to claim a certified senior-skill review; without it, explicitly
label the result advisory. Main spot-checks and records disposition before code.

## Astra design advisory disposition before production

Local requested-Astra Max verified the brief/references and prioritized the
mutable callback ownership boundary over additional rendering. Effective
settings metadata is unavailable, so this is an advisory, not a certified
senior-skill pass. Main spot-checked retirement/replay ordering, raw parent
conversion in state packing, classic owner omission gates and `SV_DropClient`.
No human decision remains. The table below supersedes proposed decisions above.

| Recommendation | Disposition / adopted implementation contract |
| --- | --- |
| Merge callback lifetime and classic sorting decisions | Adopt one raw-self/other context helper, retain callback entity, preserve QC mutations, determine custom eligibility after callback. Retain recipient throughout snapshot/classic pass, and check free after every callback. Do not replace native sorting or add a state cache. |
| Classic gather-abort cleanup cannot use an unbuilt sorted list | Adapt: release admitted count through `sort ? net_edicts : net_edicts_sorted`, including the separately retained owner once, after normal serialization, overflow or gather abort. Before serialization recheck free/non-owner model validity and model limit; later callbacks may have changed earlier candidates. |
| Freed recipient must abort safely through existing caller | Adopt spawned network-recipient checks at presend and send entry, and after customization/SendEntity. Unwind globals/retains/message limits and publish any reallocated scratch owner with zero valid states before failure. Caller bound to recipient uses `SV_DropClient(true)`; false would invoke QC/convert a freed player. No packet/delta commit after failure, no drop inside a helper still using frame arrays. |
| Preserving only snapshot no-remove bits misses later dropped-frame replay | Adapt: hidden live custom eligibility clears CURRENT/USABLE and preserves PRESENT/REMOVE/WAIT/RETIRENEW. Existing writer discards USABLE when CURRENT-clear before classifying work, honoring explicit REMOVE. Visible re-entry forces full update from prior CURRENT-clear. No PVS re-query or new hidden-state list in writer. |
| Parent validation must cover PVS bypass and state packing | Adopt shared raw-offset allocation/alignment/liveness check plus bounded parent walk. Ordinary invalid/cyclic attachment is not admitted. Mandatory owner keeps record with invalid outgoing attachment suppressed, without editing QC fields. `SV_BuildEntityState` also uses safe direct-parent validation, since server/client makestatic call it independently of snapshots. |
| Reference pvsflags timing differs | Choose QSS-M post-customization sampling explicitly, so callback field mutations affect this recipient's visibility. Do not describe primary pre-callback sampling as equivalent. |
| Mandatory native owner survives all omission gates | Adopt live owner callback/field mutation with modern requested-hide EXTERIORMODEL; keep classic owner through filters and zero-alpha gate. This hides ordinary modern geometry, not every effect/overlay; classic has no equivalent flag. Preserve prediction/solids metadata and existing model values. |
| Native client attachment scope was overstated | Corrected: parent transforms/flag inheritance exist, tag-index transform remains TODO and client chain depth ten. Do not import a renderer on that premise. |

Additional main evidence: `pr_edict.c:128` excludes reserved world/client slots
from the reusable FIFO. Thus entry/post-callback free checks do not require a
new player generation to detect ordinary allocation reuse. `PF_Remove` itself
does allow ED_Free, so recipient death must be handled explicitly. Static state
packing callers in `pr_cmds.c:1795/1976` use the current VM; safe parent-offset
validation must be current-VM generic, not hardcoded to the server allocator.

One bounded coding worker owns only `Quake/progs.h`, `Quake/server.h` and
`Quake/sv_main.c`; no client/renderer changes. Copy four field declarations and
six PVS constants, shared callback/reference/PVS helpers, adapt existing modern
and classic loops, consume existing custom CURRENT before replayed dirty work,
and propagate recipient failure at the existing send callers. Preserve stable
removal boundary and packet framing. Include native server admission of explicit
emission metadata and alpha-zero entities with trail/emission as references do;
no model-less client rendering claim. No trace/PHS service or mod-specific rule.

Estimated under300 production lines (updated from250 before code for both
packing callers and complete recipient-failure cleanup); reopen beyond300 or if
a parallel state/lifecycle/cache appears. Main reviews every change and requests
bounded Astra source recheck on the integrated result. Builds/tests/compiler
checks/probes remain deferred. Full NET-003 visible behavior qualification is
not established by this design disposition.

## Source implementation and integration checkpoint

`72510659` implements the adopted adapter in the three planned files: four QC
fields, six PVS constants and the existing server snapshot/send owners (210
insertions, 68 deletions). Main inspected the actual diff and native PVS helper,
current-VM references, field registration, cleanup lists and failure callers;
scoped `git diff --check` passed. The bounded coding worker is closed.

The follow-up local requested-Astra advisory inspected that exact commit against
`2e9a9338`, found no actionable P1/P2 and recommended bounded source acceptance
without reopening the architecture. Main adopts that recommendation:

| Reviewed invariant | Main source disposition |
| --- | --- |
| Callback globals/retention unwind before recipient failure | Accept: customization borrows raw self/other; SendEntity cleanup precedes false return (`sv_main.c:2398`). Existing callers use crash-drop for a freed recipient after helper ownership is released. |
| Snapshot scratch owner survives failed collection | Accept: current pointer/capacity published and valid count zeroed before release/return (`sv_main.c:2779`); presend does not calculate deltas on failure. |
| Classic gather cleanup works before sorting and after overflow | Accept: original admitted list selected by sort flag, each admitted entity/recipient released once (`sv_main.c:4245`). Serialization checks free/model/name/limit again. Native sorter remains. |
| No-remove is compatible with loss replay | Accept: CURRENT/USABLE clear on hidden live custom mapping; CURRENT-clear writer masks replayed USABLE before work classification. Explicit removal debt and stable ACK boundary remain; visible re-entry forces full update. |
| Attachment/PVS paths share safe current-VM references | Accept: bounded/aligned allocated live-parent checks, normal/ignore/viewmodel path validation, mandatory owner invalid outgoing link suppression, standalone state packing guards. No extra cache or attachment renderer. |
| Native owner and graphics boundaries remain | Accept bounded source contract: mandatory native record retained across omission gates, modern ordinary geometry flag and classic limitation explicit; no packet format, renderer task or Vulkan changes. |

Reserved client-slot exclusion (`pr_edict.c:128`) and QC-bypassing crash-drop
(`host.c:578-606`) were directly inspected for the design and remain assumptions
of this follow-up scope. Effective settings metadata is unavailable, so this is
an advisory, not a certified senior-skill pass. No builds, tests, compiler checks,
runtime probes, fixtures or performance measurements ran. The final Linux/ARM
software checks and actual scripted effects/visibility qualification remain
deferred; user live headset/multiplayer/performance tests are outside the goal.
This source checkpoint does not certify full NET-003 parity or full migration.
