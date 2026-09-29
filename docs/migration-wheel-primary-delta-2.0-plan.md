# Current-primary weapon-wheel reconciliation

Status: plan preceded implementation; stage1 stock preview paths and required
held-identity consumer adapter and association/fallback corrections pass final
local Astra source review. Build/draw qualification remains end-of-goal work.
Stages2..4 remain pending.
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
filtering remain unchanged. Catalog consumers do not yet read the snapshot;
the subsequent identity/partial-overlay stage remains required. Final local Astra Max source review accepted this bounded prerequisite.
End-of-goal qualification and catalog consumer integration remain pending.

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
