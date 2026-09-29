# Current-primary weapon-wheel reconciliation

Status: plan preceded implementation; stage1 stock preview paths and required
held-identity consumer adapter and association/fallback corrections pass final
local Astra source review. Build/draw qualification remains end-of-goal work.
Stage2 authored identity/partial-overlay consumers pass final local Astra
source review. Stages3/4 and full implementation qualification remain pending.
Reference: product primary `master` at
`51b452c018273647dcf94f4628a370267ff8fa91`, read-only sibling
`quakespasm-openvr`. The [source-update record](migration-source-updates.md)
identifies this delta separately from the original inventory pins.

## Behavior and verified gaps

Use the primary's native weapon identity and roster behavior in both desktop
and OpenXR wheels while retaining the existing `2.0` catalog, interaction,
stable-ID, schema/calibration and Vulkan presentation owners.

- Correct stock preview paths: primary `vr.c` uses `progs/v_shot.mdl` for the
  shotgun and `progs/g_shot.mdl` for the super shotgun. Current `2.0`
  `vr_weapon_menu_stock_entries` uses the older `g_shot` / `g_shot2` paths.
- Primary `vr_weapon_catalog.h:32` supplies descriptor-based identity
  compatibility. Commands/models do not themselves prove equal identity.
  Existing `2.0` schema matching still rejects command differences and uses
  model/selector fallback; incompatible explicit descriptors must remain
  separate, and ambiguous overlays must be diagnosed rather than first-match.
- Primary separates held-model identity from wheel preview, tracks authored
  fields, and lets partial declarations enrich known records. `2.0` already
  has model provenance and partial-schema metadata; adapt those instead of
  transplanting the primary's monolithic `vr.c` catalog.
- Primary's own-game `roster complete` declaration suppresses unrelated stock
  guesses. `2.0` already filters inherited `wwheel.txt` by search-path identity,
  but its shared schema has no complete-roster declaration.
- Primary AD/Enyo upgrades change the native parent-slot preview. `2.0`'s Enyo
  profile still declares AV72 as a separate slot. Preserve inherited profile
  behavior through the existing table/preview owners, with no new mod-specific
  gameplay implementation or physical/native weapon adapter.

The primary is behavioral evidence; its complete code is not automatically
correct for Vulkan model loading, per-frame prepared draw data or shared desktop
presentation. Existing `2.0` hover/retry IDs must survive an updated preview.
Unknown weapons retain explicit native-command/ownership declarations; discovery
cannot infer inventory from unrelated keys, armor, upgrade or secondary bits.

## Minimal adapter and ownership

Choose field/identity extensions in `vr_weapon_menu.c`, its existing public
entry type, `vr_weapon_catalog.h` and shared schema parser/entry declaration.
Reuse selection retries, model provenance, game/map reset, 3D frame preparation,
ammo/stat readers and calibration overlays. Do not add a second catalog, model
registry, parser, generic command-alias interpreter or per-mod renderer.

Preserve all current held/muzzle/melee fields, including `melee_frame`; a new
roster flag must not become another calibration owner or disappear during saves.
Explicit file fields win over native roster/profile fallback; omitted fields do
not erase known values. Complete roster applies only to the active game's own
file and cannot be inferred from the presence of calibration-only entries.

## Implementation stages

1. **Stock preview correction and required consumer adapter:** copy the two
   literal preview paths from the primary and append a held-model path to the
   existing entry type. Supply the eight exact native stock held paths in the
   same stock table. Existing active/discovery consumers use that held path
   when present, otherwise retain conventional preview alias matching. Existing
   main-thread preview loading tries the known held fallback before a guessed
   basename swap. No selection/ownership policy or entity viewmodel change.
   When current schema metadata replaces a stock preview, clear its inherited
   stock held path so a custom slot cannot be constrained by the old native
   model. Full authored held metadata/provenance remains stage2, with shared
   copied storage. This small consumer adapter closes the demonstrated stock
   incompatibility without waiting for the broader catalog-policy change.
2. **Identity and provenance:** copy the primary identity helper and adapt
   existing matching/overlay and discovery owners. Record a senior disposition
   before this coupled policy change. Keep conflicting explicit descriptors
   distinct; diagnose ambiguity; preserve authored preview/held identity.
3. **Roster completeness:** extend the shared schema's existing top-level parser
   and metadata return, then adapt catalog initialization and own-game precedence.
   Do not insert a separate text scan/parser. Preserve native `wwheel.txt`
   compatibility and calibration-only files. Review public call sites and
   calibration save/preflight before changing parser output.
4. **Inherited upgrades and full source reconciliation:** parent-owned preview
   variants at existing profile owners, stock/profile/discovered visibility and
   diagnostics. Compare every primary commit hunk; explicitly record any
   intentional Vulkan/desktop adaptation. Reopen if this exceeds one tightly
   coupled catalog/schema slice or introduces another model identity database.

Stages2..4 need a verified implementation brief and local Astra review; the
two-path correction does not decide those architectures. Coding delegation
must use disjoint ownership and leave the primary branch and user edits intact.

## Verified next-slice brief: identity and partial overlays

Source inspection after the two literal path copies demonstrates why the
primary's adjacent held-identity separation is necessary: `EntryActive` and
`FindDiscoveredModel` currently compare a held model to the preview. Native
super-shotgun `v_shot2.mdl` is not a basename alias of its actual pickup
`g_shot.mdl`; substituting preview paths alone can lose active highlighting and
create a duplicate discovery row. The reopened stage1 adds exact stock held
identity at the existing entry/active/discovery/preview-loader boundaries;
do not qualify the literal preview paths independently of those consumers.

The proposed tightly coupled write set is `vr_weapon_menu.c/.h`,
`vr_weapon_catalog.h` and `vr_weapon_schema.c/.h`. Keep current catalog arrays,
stable IDs, generation and prepared assets; no new registry or parser. Reuse the
primary's identity helper. Add distinct held identity to the existing public
entry, with stable copied storage at the same catalog owner for authored or
learned strings. Existing stock definitions supply exact held paths. Main-thread
matching/discovery checks held identity, and main-thread preview loading tries
the explicit held fallback before provisional `g_`/`v_` names. Frame data remains
immutable during rendering; no per-task loading or new gameplay model owner.

The shared parser needs authored wheel-field presence (command, ownership,
active, ammo, preview, held identity, scale, offset and ammo maximum) because
default scale1/offset0 cannot identify an omitted field. Mark presence only when
parsed, not when `FinishEntry` infers model aliases or active-to-owned fallback.
Preserve the existing calibration finish behavior for its consumers. The wheel
must not treat that inferred ownership as an explicit descriptor. Reuse presence
when merging file overlays, retaining native values for omitted fields and
explicit values, including scale1/offset0, when supplied.

Catalog initialization should form stock/built-in native definitions first,
then enrich with own-game file declarations and native wwheel fields. Explicit
vr_weapons fields have precedence; wwheel can supply missing selector/command/
ammo/ownership but cannot erase authored geometry or incompatible descriptors.
Do not broadly infer inventory from active-only declarations merely because the
calibration parser retains that historical fallback. Ambiguous overlay matches
must stop that overlay without allocating a duplicate via another first-match
path. Identity conflicts retain distinct entries even with a shared impulse.
Profile stock-replacement is deliberate fallback replacement, not permission
to merge two conflicting explicit schemas.

This brief does not authorize complete-roster syntax or AD/Enyo variants yet;
those remain stages3/4. Before implementation, local Astra must decide whether
this initialization-order adapter fits existing catalog ownership and specify
any smaller boundary. End-of-goal checks must cover observed stock
`v_shot2`/`g_shot`, explicit different held/preview names, calibration-only
partial overlays, conflicting descriptors and stable IDs, not just helper tests.

## End-of-implementation acceptance

No builds/tests until the full implementation pass is complete. Then qualify
both Linux architectures: stock previews before equipping, desktop/VR wheel
selection, partial offsets/command overlays, complete custom roster, inherited
file rejection, identical commands with conflicting explicit identities,
different held/preview names, ambiguous overlays, map/game resets, stable hover
IDs, native upgrades and unavailable models. Exercise actual selection/stat
acknowledgement and prepared Vulkan drawing; isolated helper checks alone do not
prove user-visible parity. Live headset trials, Windows builds and performance
measurement remain user-deferred.

At this checkpoint the broader catalog delta is pending. No full current-primary
wheel parity claim follows from correcting two preview paths.

## Local Astra disposition (2026-09-29)

Wegener reviewed the actual stock adapter at `6f6f5f4a`, against primary
`51b452c0` held/preview matching, overlay and learning consumers. Stage1 is
accepted with three narrow corrections: preserve a known held association when
schema metadata repeats the same preview; clear inherited held identity when
profile enrichment changes that preview; likewise clear it when learning
replaces the preview. Determine change before writing shared path storage.
These are source findings, not a claim of live asset qualification.

| Stage2 decision | Disposition |
| --- | --- |
| Catalog initialization | Revise the proposed order: parse own-game `wwheel.txt` into the existing empty catalog first; if no valid roster, seed stock; then apply built-ins and file overlays. Parsing wwheel after seeded rows conflicts with existing duplicate-ID/count/clear policy. |
| Authority | Only successful own-game wwheel is authoritative in this stage. Partial schema files retain omitted native rows. Complete-roster syntax remains stage3. |
| Authored values | Track each key separately (selector, command, stat/mask halves, ammo, explicit maximum, preview, held, scale, offset). Preserve raw values through normalization; a presence bit alone cannot recover explicit zero overwritten by inferred ammo defaults. Keep parser compatibility and calibration finish behavior. |
| Identity | Reuse primary descriptor compatibility; commands are not identity. Rank exact held match before exact preview among compatible candidates. Return MATCH/NOT_FOUND/AMBIGUOUS; ambiguity is terminal, with no fallback search or allocation. Resolve omitted descriptor halves only after unique matching. |
| Ownership/capacity | Explicit ownership disables legacy bitmask supplements. Recompute capacity metadata when ammo type changes. |
| Storage and IDs | Retain existing arrays and collision-checked ID allocator. Copy held paths into stable catalog storage. Calibration-only unmatched fragments create no wheel rows. |
| Learning | Track held identity separately from preview provenance and preserve authored preview. |
| Scope | Existing catalog/schema owners only. Complete rosters and AD/Enyo parent upgrades remain stages3/4; no new gameplay adapter. |

Stage2 implementation follows this disposition, superseding the stock-first
proposal above. End-of-full-implementation checks must cover the actual stock
SSG consumer, partial overlays with/without wwheel, explicit zeros/defaults,
descriptor halves/conflicts/ambiguity, stable IDs and calibration persistence.
No builds/tests have been performed for this slice.

### Stage2 parser prerequisite: provenance before normalization

Verified directly in primary `master51b452c0` `vr.c:8930..9025`: authored
wheel keys set field bits at token parsing, while overlay at `8785..8819` only
replaces those fields. Current shared `VR_SchemaFinishEntry` infers held/preview
aliases and active-to-owned descriptors for calibration; the `ammo` token also
fills a zero maximum before finishing. Calibration parse/save consumers rely
on that existing finished view. A presence bit added afterward cannot restore
an explicit zero maximum, an explicit empty preview, or an owned-mask half
that finishing replaces. This is the demonstrated narrow incompatibility.

Proposed adapter: append one parser-owned wheel declaration snapshot to the
existing schema entry: per-key presence plus raw preview/held, selector/command,
ownership/active stat and mask halves, ammo type/maximum, scale/offset. Capture
before finishing; an explicit ammo maximum is recorded when read so later
inference cannot overwrite it. The existing parser signature, acceptance,
staged atomic commit and finished calibration values remain unchanged. No new
text scan, parser, runtime catalog or persistent model owner. The subsequent
wheel merge reads this metadata; calibration keeps reading current fields.
Expected parser/header change under120 lines, followed by the reviewed existing
wheel consumers. No production partial-overlay behavior changes in the parser
prerequisite alone.

Alternative considered: a new raw-parse mode/API returning unfinished entries.
It reduces duplicated scalar storage, but risks diverging validation/filtering
and presents two meanings for the same existing entry. A parallel parser is
rejected. The snapshot duplicates only declaration data (bounded64 entries),
not catalog state or identity policy. Reopen if it requires calibrator edits or
another registry. Local Astra must disposition this representation before edits.
End-of-goal acceptance includes unchanged calibration outputs/failure atomicity,
per-key omission, explicit zeros/defaults/empty paths, both ammo-max token orders,
and no inferred alias/ownership marked authored. No builds/tests now.

The stage1 source review additionally verified primary `VR_WeaponPreviewModel`
(`vr.c:12162..12188`): guessed g_/v_ basename fallback is permitted only when
held identity is absent. Adopted at `VR_WeaponMenu_PrepareModels`; if both known
super-shotgun preview and held meshes are missing, do not guess the regular
shotgun. The three association-lifetime corrections are source-accepted; the
remaining fallback guard receives a bounded final check. Asset availability
and actual drawing are end-of-goal qualification, not inferred source results.

Stage1 final local Astra Max source review accepted the guarded fallback.
The parser-prerequisite representation is also accepted: append inline value
storage and13 per-key bits to the existing schema entry, set bits only after
successful reads, snapshot before finishing, and retain explicit ammo maximum
when its token is read. `ammo` and `ammo_stat` have separate presence bits but
share the final authored type value. No token/source pointers may escape.
Existing filtering, acceptance, finished calibration values and failure-atomic
staging are unchanged; missing bits mean non-authoritative snapshot values.

Astra verified the size tradeoff: roughly180 extra bytes per entry and11.25KiB
per64-entry array, potentially34KiB additional nested save-verification stack.
This is bounded declaration metadata, not another catalog. Static built-ins
remain zero-metadata values consumed through their existing finished fields.
Appending preserves existing member offsets but changes size/array stride;
rebuild dependents during end-of-goal qualification. No binary compatibility
with stale objects is claimed. Raw-mode/new API is rejected for this slice.

The bounded parser prerequisite is now implemented.
Its inline snapshot captures parsed declarations before calibration finishing,
including explicit ammo maximum at token read. All13 presence bits are set
only by successful authored reads; omitted keys and inference add no bits.
Calibration's finished values, globals, aliases, held/melee fields and staged
filtering remain unchanged. Final local Astra Max source review accepted this
bounded prerequisite. Stage2 catalog consumers now read the snapshot; final
source acceptance is recorded below. End-of-goal qualification remains pending.

## Stage2 consumer implementation contract

Continue the accepted identity/provenance design using the raw snapshot now
implemented in `764cea68`. Verified existing owners: `ApplySchemaMetadata` and
`AddSchemaEntry` currently infer finished values; `ApplySchema` merges every
first matching wwheel row or creates a replacement partial roster; profiles
currently follow file overlays; discovery has ambiguous `-1` outcomes that can
fall through to new allocation. These are the next demonstrated boundaries.

Reuse the primary identity helper verbatim in the existing catalog header.
Keep one catalog, stable-ID allocator, model provenance and prepared frame.
Append authored-field bits and native-profile provenance to existing entries,
and copy authored/learned held paths into the existing catalog owner's bounded
storage. The native-profile bit preserves the primary's independent
`game_profile` information after a file overlay marks a row schema-derived.

- Parse own-game wwheel into an empty catalog; seed stock only if absent or
  invalid. Built-ins follow, then own-game file overlays. Successful wwheel
  authority remains independent of partial files; complete rosters remain stage3.
- Match declarations using actual selector/full descriptor relationships; a
  command never proves identity. Check authored descriptor halves separately.
  Missing halves are constraints rather than new identity evidence and are
  resolved only after a unique match. Unauthored stock descriptor defaults may
  be replaced when another known identity relationship identifies that slot;
  conflicting explicit/native-profile descriptors remain distinct. Rank exact
  held match before exact preview, then current source provenance. Ambiguity
  stops the operation, including runtime discovery; no alternate first-match
  search or allocation follows it.
- Merge only flagged raw values. Keep matched IDs; use the current collision-
  checked allocator for new declarations. Unmatched fragments need a valid
  authored native command and complete usable identity; unresolved descriptor
  halves and calibration-only fragments create no rows. An explicit ownership
  declaration disables both implicit selector ownership and legacy supplements.
- Recompute native ammo/capacity fallback when ammo type changes; preserve
  omitted values and explicit maximum, including zero. Explicit maxima must not
  accidentally use a capacity stat from the former ammo type.
- Learn held identity independently of preview provenance. Authored preview is
  preserved. Authored/native-profile held identity cannot be overwritten by an
  unrelated observation. Prepared models and frame strings stay under the
  existing main-thread load/frame-copy owners.

Write set: `vr_weapon_menu.c/.h`, `vr_weapon_catalog.h`; parser and calibrator
unchanged in this stage. Expected400–750 changed lines across the existing
matching/overlay/discovery functions. A separate catalog/parser, protocol,
entity viewmodel owner, command interpreter or per-mod gameplay layer requires
reopening. Astra source review follows integration; builds/tests remain deferred
until full implementation. Stage3/4 and device-owner reconstruction remain open.

A direct primary documentation cross-check clarifies stage2 ammo precedence:
`docs/vr-weapon-wheel.md` says runtime capacity extensions take precedence over
a fixed maximum; `VR_WeaponCatalog_ResolveAmmoMax` already implements that
policy in `2.0`. Preserve it. Explicit maxima, including zero, remain the
stored fallback and are not overwritten by profile defaults; dynamic capacity
must be derived from the current ammo type, not disabled merely because a fixed
maximum was authored. No new capacity-policy owner is needed.

## Stage3 verified brief: own-game complete roster

Primary `master51b452c0` `vr.c:8896` consumes `roster complete` outside blocks
and applies authority only when the file belongs to the active search path.
Its current weapon-wheel documentation describes explicit complete rosters as
suppressing undeclared generic stock guesses; partial offsets/command files
must not imply completeness. Current shared parser ignores the unknown global
words, so that declaration has no metadata consumer here. Calibration saving
copies text outside blocks unchanged, validates effective values/counts before
writing, and verifies active-game output afterward (`calibration.c:982..1070`).
These are source facts; no end-to-end save result is claimed.

Choose one optional metadata output on the existing shared parser. Retain the
four-argument `VR_WeaponSchemaParse` wrapper for current consumers; add a
metadata-capable entry point using the same tokenizer, globals, validation and
staged commit. Add one `complete_roster` flag, set only by a successfully read
`roster complete` directive; never infer it from count, calibration fields,
preview paths, command presence or unknown words. No separate scan/parser or
catalog authority owner. Preserve primary case behavior (`roster` key exact,
`complete` value case-insensitive), sticky complete declarations and unknown
scalar values that do not establish completeness. Missing/brace values fail
parse without publishing metadata. Output defaults false on failure.

The wheel's existing own-game loader obtains the metadata after its search-
path check and can accept a valid complete declaration with zero entries.
Set existing catalog authority even when that file is empty; force the mutable
catalog active so suppression cannot be bypassed by the immutable stock route.
Reuse `ShouldExpose`: undeclared stock rows are suppressed; authored matched
rows, known native profiles and actual unselectable discoveries follow existing
source policy. Do not discard calibration entries or rewrite their ownership.
Partial files and successful own-game wwheel retain stage2 behavior.

Calibrator parsing continues through the compatibility wrapper for ordinary
loads. Its save/preflight uses the metadata-capable parser to compare original
and rewritten complete-roster state alongside the current effective-value/count
check. Keep surgical text preservation and existing post-write verification;
do not serialize a new file or add another save path. No metadata in a weapon
block or protocol. The user does not need old unrelated settings preserved.

Write set: `vr_weapon_schema.c/.h`, the existing loader/reload functions in
`vr_weapon_menu.c`, and the bounded save/preflight calls in
`vr_weapon_calibration.c`. Expected80–160 changed lines; no parser/calibrator
rewrite, source-file reparse pass, authority state machine or new renderer.
Local Astra disposition is required before stage3 production edits. This brief
is independent read-only planning while stage2's actual diff is source-reviewed.

End-of-full-implementation acceptance: own/inherited complete declarations,
partial files, zero-entry complete files, omitted/inferred fields, malformed
values and failed parse atomicity, game/map reset, authored stock visibility,
calibration save retaining directive/comments/unknown tokens and exact requested
held/muzzle/melee values. Linux/ARM checks follow full implementation; no builds,
tests or fixtures now. Stage4 native parent-slot variants remains separate.

## Stage2 local Astra implementation disposition

Sartre reviewed the actual744-line consumer diff, primary helper/callers and
capacity amendment. Accepted with four P2 corrections; no P1 or new registry/
state machine, and the existing architecture remains suitable.

| Source finding | Main disposition |
| --- | --- |
| A flagged ownership-mask half can be overwritten while its stat is unset. | Adopt: protect an authored mask independently of its stat half. Unknown native fallback masks remain replaceable. Apply the same rule to active halves. |
| Stock-to-profile replacement preserves the unchanged-preview held alias across memset, then clears it because the comparison lost the old preview. | Adopt: retain the old preview comparison input through replacement. Unchanged Dwell SSG keeps g_shot/v_shot2 association; changed native previews still clear inherited held guesses. |
| Runtime learning checks selector without the declaration's actual active stat. | Adopt: extract the existing stat predicate and reuse it in both discovery searches and active detection. Keep exact selector fallback for entries without active descriptors. Inactive declarations cannot absorb another weapon. |
| A max-only file overlay on a wwheel nails row misses dynamic capacity. | Adopt: derive capacity metadata from the final ammo type at wwheel creation and after each metadata merge; preserve explicit fallback zero. |
| Model-provenance enum/array now have writers and no readers. | Adopt simplification: delete obsolete write-only policy bookkeeping. Authored fields, native-profile provenance and existing copied model/held paths already govern behavior; no replacement array or policy owner. |

Final bounded source review follows these corrections. Identity/overlay source
acceptance does not prove actual selection, render/asset availability or full
native rosters. Complete-roster/upgrade work and end-of-goal Linux/ARM checks
remain required. No builds/tests/fixtures have run for this stage.

The corrections and provenance deletion are now integrated. Final source
scope is792 changed lines against the400–750 estimate; the42-line overrun is
bounded to sharing the existing activation predicate, preserving/resetting
existing comparison inputs, and deleting the obsolete provenance enum/array
and writers. It adds no new owner or architecture. Final Astra review accepted
these corrected source paths and judged no architecture reopening necessary.
The final mask correction compares authored active halves against stored values,
not selector-normalized identity; repeating a half declaration cannot erase it.
Native-profile model discrimination now survives source changes to SCHEMA.
The
capacity fallback remains stored exactly, including explicit zero, while
current-type runtime capacity retains primary precedence.

## Stage3 local Astra design disposition

Sartre accepted the metadata adapter design with one P2 source-path correction
before production. Own-game filtering in the wheel alone is insufficient:
the existing saver resolves an inherited file, copies text outside blocks, then
writes it into the active game. Preserving `roster complete` would promote
previously ignored inherited authority. Adopt the narrow fix: retain load
path_id and choose the existing own-game file or empty save baseline if the
resolved source is inherited. Ordinary inherited calibration loading remains
unchanged; only the requested calibration block is created in the active game.
The user explicitly permits dropping unrelated legacy settings. No token-removal
scanner, extra parse pass or second save path. Compare roster metadata from the
selected baseline with rewritten output, and retain post-write byte verification.

| Decision | Disposition |
| --- | --- |
| Optional parser metadata | Accepted: one staged flag, existing tokenizer and four-argument compatibility wrapper. Failed parses publish false metadata. |
| Syntax | Accepted: primary exact key, case-insensitive/sticky complete value; malformed missing/brace values fail under the shared validated grammar. |
| Empty complete declaration | Accepted: reuse existing mutable-catalog activation and authority, never infer completeness from count. |
| Calibration save baseline | Corrected as above: own-game or empty; inherited authority must not be promoted. |
| Save verification | Accepted: extend the two existing parses, effective-value/count comparison and exact post-write verification. |

No P1, additional owner or architecture reopening. Production source review
and Linux/ARM qualification remain required; no builds/tests/fixtures now.

## Stage4 verified brief: native rosters, variants and diagnostics

This is planning, not stage4 implementation or acceptance. Direct read-only
reference remains primary master `51b452c0`, not old feature-map assumptions.
`vr.c:1562..1887` supplies current native definitions; `1245..1340` resolves
held/preview variants and model discriminators; `11980..12143` applies peer
visibility and prints `vr_weaponlist`. The current 2.0 tables and consumers in
`vr_weapon_menu.c` demonstrate these remaining mismatches:

- AD's eight-slot native roster is absent. Copy its exact pickup/held pairs,
  commands and ITEMS/ACTIVEWEAPON descriptors. Only primary's existing exact
  `ad` inventory gate is established by this evidence. AD asset/calibration
  family detection proves geometry, not another mod's QuakeC inventory or
  upgrade flags; do not expand gameplay gates from that signal.
- Dwell's axe held identity is v_axe2; its other exact held paths and permitted
  v_axeb/v_nail3 variants must survive profile construction. The current table
  lacks the axe profile. Retain existing primary Dwell-family gating.
- Alk/limjam use WEAPONS ownership, including stock-looking bits that overlap
  unrelated ITEMS. Copy native held paths (including 20/40fps names); correct
  saw impulse to226 and mine to229 from the actual primary table. Enyo also
  uses WEAPONS, and its launcher previews spell glaunch/rlaunch.
- Enyo's AV72 is an upgrade of selector4 using WEAPONS flag16384, not the
  current independent selector1024/impulse5 row. AD axe grapple/shadow variants,
  SSG Widowmaker and lightning plasma use MODITEMS128/4096,2,64 respectively.
  They change held/preview paths without allocating slots or altering native
  parent ownership/commands. Explicit held and preview keys independently win.
- Primary marks ad, alk/limjam, enyo, qbj3 and Dwell verified complete. Preserve
  current wwheel and complete-file authority under the existing catalog flag.
  Do not mark partial Mjolnir/MG3/expansion additions complete. Physical dual-
  state Mjolnir behavior remains outside scope.
- Current peer fields are not computed; primary compares descriptor identity
  before suppressing fallback rows and known-slot observations. Current2.0
  also lacks `vr_weaponlist`, which exposes these exact decisions to users.

Minimal adapter: extend the existing native profile row with its known held
path and update its literal tables, rather than add another roster registry.
Use primary's existing native variant switch in two small path-resolution
helpers within this same catalog owner. Pass current stats where needed; do
not mutate schema fields, IDs, source provenance or commands on upgrade change.
Reuse those helpers at active/discovery matching and main-thread preview loads.
Accept primary's known model variants only for native profiles without authored
held keys; no arbitrary basename inference of upgrade ownership.

The existing preview cache is indexed by stable row and remembers missing
models (`PrepareModels:1576`). A variant can change without a game/session
generation change. Retain the resolved preview/held pair in that cache and
clear just that row's model/geometry/missing marker when either path changes;
load through existing Mod_ForName before worker preparation. Copy resolved
preview strings into the existing immutable frame storage (`:2127`). No loader
or renderer branch, second model cache, asynchronous catalog mutation or new
per-frame allocation. Validate cache/fallback selection against the same paths
used by active/discovery matching, including independently authored overrides.

Extract the current visible-row decision once for the existing BuildVisible
consumer and new diagnostic command. Compute compatible source peers using the
copied primary identity helper, preserving conflicting declared identities and
stable rows; hide observed duplicates of an owned non-authoritative stock slot
as primary does. Register the diagnostic next to existing weapon-menu commands
in `cl_input.c`, with one public declaration in `vr_weapon_menu.h`. It must use
the same ownership/active/visibility functions, not reimplement selection or
load assets. Diagnostic output includes source/visibility reason, selector,
command, ownership/active stat/mask/value, held and resolved preview paths.

Write set: `vr_weapon_menu.c/.h`, bounded command registration in `cl_input.c`.
Expected250–450 changed lines. Separate inventory/upgrade state, AD gameplay
gates inferred from identical meshes, or repeated per-mod gameplay additions
requires reopening. Existing calibration, native QC, rendering, networking,
loading and immutable frame owners remain reusable. Local Astra must review
this verified brief before stage4 production changes.

After all implementation: qualify literal native roster/command/held tables,
partial authored overrides, empty ownership, parent upgrades while wheel is
open, missing preview with exact held fallback, source peer conflicts,
observed known slots, game/map reset and diagnostic/visible-row agreement.
User-owned live VR/performance tests remain outside the implementation goal.
