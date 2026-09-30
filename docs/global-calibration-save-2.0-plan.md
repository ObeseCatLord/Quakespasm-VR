# Global classic weapon calibration through existing schema and slot owners

2026-09-30. Restore the inherited global-save action on2.0 without creating a
second calibration registry, schema loader, renderer coordinate system or
multiplayer offset set. Plan precedes implementation. Only Quake/
vr_weapon_calibration.c is the proposed production write set; main/master and
the user's dirty docs/migration-2.0.md remain untouched.

## User-visible contract and actual reference

The primary51b452c0 vr.c:6064–6192 writes three classic globals from the active
slot and copies base held/scale/muzzle values to its loaded schema slots. Its
filter at5173 preserves enhanced fields. Earlier planning claimed it stripped
enhanced fields; rereading the actual filter after the Astra advisory corrected
that claim. Enhanced geometry has an independent calibration family and its
held lookup does not use classic scale/offset. This feature restores classic
global calibration, not a new enhanced-global format or geometry conversion.

Provide vrweaponsaveglobal at the native save-command owner. With a valid active
classic alias viewmodel, save its current held scale/offset and muzzle offset
for the known calibration roster. Preserve enhanced, melee and muzzle-source
settings, local authored wheel descriptors, complete-roster policy and unrelated
text/comments. Publish the selected classic values only after verified saving.
The solo/multiplayer rendering and shot offsets stay shared. Reject enhanced
invocation honestly with guidance to the existing vrweaponsave; do not claim a
visible MD5 calibration was globally applied when only hidden classic cvars
changed. Reject while a controller adjustment preview is active: its later
commit/cancel owns the preview, including removal of a newly allocated slot.

Saving must work without a preexisting local schema and when calibration was
loaded from an inherited schema. Do not narrow the action to the current weapon
in those cases. The target set is the bounded union of accepted local-schema
viewmodel identities, registered live calibration identities and active identity.
No model discovery/load, mod-name whitelist or arbitrary dormant roster scan.
An explicit save freezes calibration for the known roster; the globals also
remain available to subsequently appended authored schema blocks. It does not
promise automatic calibration of every unknown future runtime model.

## Verified native boundaries and minimal adapter

Native calibration.c:883–1090 already owns current-model connection/precache/path/
format/finite validation, safe bounded text output, local authoring, parse
preflight, COM_WriteFile and exact active-file readback. Controller commits reuse
that single-save action. Preserve its APIs and ordinary behavior; share admission
and IO by adding an internal global-save mode rather than copying a second save
pipeline. Global mode must not perform the single-save temporary muzzle-presence
mutation before preflight. Its own output writes an explicit finite muzzle value.

The span-based WriteUpdatedBlock and text helpers at423–718 already preserve
comment/quote/compact-brace syntax and unknown-key arity. Extend their selected
classic path to force explicit muzzle output for global saves while preserving
ignored MP text in that mode. Ordinary single-save behavior remains. Remove only
the three old classic-global directives from top-level spans with native token
arity; no new line/brace parser or whole-entry serializer for existing text.

Schema.c:248–298 applies globals forward to missing fields and may synthesize
viewmodel identity; :576 can accept a formerly ignored model-only block after
new globals. To avoid promotion, preserve original block order and put replacement
global directives AFTER the original blocks. Accepted target blocks receive
explicit requested values using the existing block writer; missing known-slot
calibration blocks follow the globals. They inherit the same values. This output
ordering differs deliberately from the primary's prefix, preserving native
acceptance instead of introducing parser provenance or per-block policy flags.

The wheel owner ignores inherited vr_weapons.txt (menu.c:653–663); calibration
reload does inherit it (calibration.c:2056–2067). Copying a complete parent file
into this game would newly authorize its wheel/complete roster. Instead, newly
materialized blocks contain only viewmodel identity and calibration fields.
Snapshot their existing unselected enhanced/muzzle-source/melee slot fields so
the new local file does not discard currently effective inherited calibration
on reload. Never copy parent wheel/impulse/ownership/preview scale/offset or
complete-roster metadata. This is an explicit calibration snapshot, not a new
inheritance/merge service; later parent edits do not update that saved snapshot.

Main read the actual wheel consumer: SchemaCompatible ends in descriptor
IdentitiesCompatible (:455), and AddSchemaEntry (:606) refuses a new row without
impulse and ownership/activation. A viewmodel-only calibration block has neither
a descriptor relationship nor authority to create a selectable row. No wheel
production change is needed for these snapshots; preserve original local wheel
snapshots and metadata exactly through the native parser.

## Implementation and preflight stages

1. Share current save admission/IO with internal global mode and register the
   new local command. Reject active adjustments/enhanced/unsafe/disconnected/demo
   cases before changes; validate positive finite scale and all selected vectors.
2. Parse valid local source with the native parser. Collect native registered
   slot identities and deduplicate the target set. Preserve source outside the
   three selected globals. Rewrite accepted target blocks via the existing writer;
   do not edit ignored/unmatched blocks or move original blocks. Match only
   accepted original calibration identities, not every known live slot; ignored
   blocks before terminal globals remain ignored. Correct the shared block matcher
   to consume scalar/vector values using the writer's schema arities, so an unknown
   key's value named `viewmodel` cannot be mistaken for an identity declaration.
3. Append the three selected globals and missing known-slot calibration-only
   snapshots, with a leading newline before globals so an EOF line comment cannot
   swallow them. Validate the private snapshot's fields using existing native finite
   predicates; no silent roster or schema-capacity truncation. Local original
   records retain unselected authoring rather than serializing whole live state.
4. Parse candidate output. Compare original accepted entries in order: identity,
   wheel snapshot, unselected fields and complete_roster stay unchanged. Only the
   counted missing known identities may be appended. Every targeted identity must
   resolve all three selected values. Legitimate shared viewmodel records remain:
   rewrite every matching accepted block, preserve original multiplicity and
   publish once per identity. Reject duplicate live slots or inert-record ambiguity
   and
   unexpected accepted-record promotion before writing; do not add another parser
   merely to handle an ambiguous file. Ensure newly added records have no wheel
   authority and snapshot unselected values and presence flags agree, including
   enhanced offsets, muzzle-source offset/viewofs, spawn_at_self_origin and all
   melee fields. Emit authored false/zero values when their presence flags are set;
   omission is not the same setting. Round-trip snapshot floats without precision
   loss; selected classic values may use the existing native comparison tolerance.
5. Extract ApplySchema's existing capacity/identity/finite preflight into a private
   reusable helper. Fill selected-only, deduplicated publication entries from the
   validated candidate's parsed values, then preflight those exact entries,
   then use existing write/readback. On success ApplySchema receives only identity
   and the three classic fields. This reuses native allocation/cvars and muzzle
   presence/seed flags without resetting/replaying enhanced, melee, source or
   unrelated unsaved live calibration. Do not call ReloadGame or load models.

Estimate reopened from <=250 to <=500 net added lines in calibration.c; the
source-review correction pass now has a550-net completion ceiling while targeting
500. At556 net, editing stopped and architecture was reopened before further
changes. The advisory verified the existing-owner architecture and identified
safe deletions: the redundant active-target pass (already included by the live
slot scan), snapshot selected-field plumbing, repeated early snapshot validation
and duplicate unselected comparison logic. Keep full utility rather than remove
roster/preservation behavior to satisfy the estimate. The
smaller local-blocks-plus-active design fails real no-file global utility; copying
a whole inherited file changes native wheel authority; a new overlay loader or
parallel state owner is unnecessary. Reopen before another production file,
parser metadata, state machine or material scope growth.

## Astra advisory disposition

A local gpt-6-astra/max review verified source and found roster/locality/acceptance
and active-preview issues. Effective settings were unexposed; this is requested-
Astra source advisory, not a certified senior-skill pass. Main spot-checked the
load-bearing reference filter, parser, calibration and wheel-consumer claims.

| Recommendation / finding | Main disposition |
| --- | --- |
| Reference preserves enhanced; original brief claimed otherwise. | Adopt correction from actual vr.c:5173. No enhanced-global feature or destructive format rewrite. |
| Local-only blocks plus active fails no-file/global utility and inherited reload preservation. | Adopt known-slot target union and calibration-only unselected snapshots; never copy inherited wheel authority. |
| Prefix globals can promote ignored blocks and alter identities. | Adapt: terminal globals plus selected explicit values in accepted blocks reuse existing writer and avoid new parser provenance. Candidate acceptance/identity/wheel checks remain mandatory. |
| Native ApplySchema owns slot allocation and muzzle flags. | Adopt extracted native preflight and selected-only ApplySchema publication after exact readback. No second calibration owner. |
| Active controller adjustment can commit/cancel different values. | Adopt explicit refusal; controller single-save paths remain unchanged. |
| Useful full action exceeds the original250-line estimate. | Reopen to500 lines before implementation, constrained to current owner. |
| Focused review verified wheel neutrality and terminal-global order, but found matcher false positives and missing snapshot/EOF guards. | Adopt shared scalar/vector matcher arity, presence-aware snapshot comparisons and leading newline before terminal globals. |
| Draft exceeded500 net lines without another persistent owner. | Reopen to550 ceiling before correction, with the safe deletions above and no narrowed roster/preservation contract. |
| Top-level span helper rejects whitespace before a block and mishandles known token values/comments. | Adopt boundary completion when token_start reaches end; tokens starting inside and crossing remain errors. Match native top-level arities: roster/held-scale1, classic/MP global vectors3, unknown tokens0. Remove only the three classic globals; preserve inter-value trivia. |
| Draft rejects all authored duplicate model records. | Adopt multiplicity-aware candidate validation and deduplicated publication; legitimate shared-model records are not inherently ambiguous. |
| An unterminated trailing block comment can swallow generated globals while candidate entries still pass. | Adopt a whole-candidate walk through the existing lexer requiring the three generated key tokens, in order, within captured emitter offsets. Native candidate parsing supplies grammar/values; no second parser or new policy state. |
| Raw float publication may differ from rounded file/reload values. | Adopt projection from already-validated parsed candidate fields, then native preflight and publication of that same array. Native cvar formatting remains. |

The focused requested-Astra source follow-up verified terminal-global semantics
and actual wheel consumers. Main adopted its preservation corrections and EOF
guards above. No parser provenance/new loader was justified.

## Source integration checkpoint

The native save-owner adapter is implemented in Quake/vr_weapon_calibration.c:
593 additions and58 deletions,535 net, within the reopened550-net ceiling.
The final requested-Astra source advisory accepted all six correction findings
with no remaining P1/P2 blocker in this slice. Main spot-checked candidate-derived
publication, preflight-before-write, exact readback and the unchanged public
single-save route. Known-roster snapshots, authored shared-model multiplicity,
terminal-global ordering and presence-sensitive unselected fields remain in the
accepted implementation. No additional loader, parser or persistent owner was
introduced.

Source review and scoped whitespace checks are not executable qualification.
No builds, tests, engine probes or live-device checks were run for this slice;
the Linux/ARM acceptance below remains end-of-implementation work. Effective
reviewer settings were unexposed, so this is requested-Astra source advisory,
not runtime/platform or whole-goal certification.

## Final software acceptance (after all implementation)

Linux and Linux ARM cover multiple classic weapons, no local file, inherited
unselected calibration, existing local partial/complete rosters, explicit old
globals, ignored blocks and shared/duplicate viewmodel identities. Include quoted
or commented braces, compact entries, unrelated wheel/melee/muzzle-source/enhanced
values, active-preview and enhanced refusal, unsafe/nonfinite values, parser/slot
capacity, write/readback failure, immediate/reload equivalence and unchanged
unrelated live state. Existing single/controller saves must remain correct.
Builds/tests/probes are deferred until full implementation; Windows, live headset/
eye tracking and performance measurement are user-deferred. No full migration
completion claim follows from this bounded source adapter.
