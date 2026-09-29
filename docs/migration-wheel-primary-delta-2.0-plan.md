# Current-primary weapon-wheel reconciliation

Status: plan preceded implementation; stage1 stock paths are source-adapted,
with build/draw qualification deferred. Stages2..4 remain pending.
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

1. **Stock preview correction:** two literal model paths in
   `vr_weapon_menu_stock_entries`, copied from the current primary. No catalog
   or gameplay change, no entity viewmodel substitution. Existing preview loader
   and held fallback continue to own resource resolution.
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
