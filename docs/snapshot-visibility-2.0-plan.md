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
  `CL_AttachEntity` already applies them and inherits VIEWMODEL/EXTERIORMODEL flags.
  No new attachment transform, renderer, entity service or culling graph is needed.
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
