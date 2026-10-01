# Classic weapon calibration presets through the existing owner

2026-09-30. Plan/verified brief before implementation. Solo engine migration;
preserve native vkQuake graphics and one per-weapon solo/MP calibration. A
bounded local requested-Astra source audit found a surviving preset-selection
gap; missing old settings alone are not the reason for this adapter.

## Verified facts and scope

| Fact | Evidence |
| --- | --- |
| Primary selector calibrates existing classic geometry rather than installing/selecting assets | [verified: primary51b452c0 `vr_menu.c:516–535`, `vr.c:3484–3504/8450–8608`] |
| Enhanced option is OSJC classic MDL conversion calibration, not rerelease MD5 | [verified: primary `vr.c:8450–8487`; native `gl_model.c:330` and enhanced calibration tables are separate owners] |
| Classic fallback omits four named choices | [verified: native `vr_weapon_calibration.c:160` stock table; primary Enhanced/Authentic/Plague/BlockQuake tables at8450/8489/8527/8565] |
| Primary live selection deliberately rebuilds classic values without reloading authored schema, retaining enhanced geometry fields | [verified: primary `vr.c:3218/3712–3768`] |
| Native schema application can preserve other fields and preflight slots, but held-only edits preserve an explicit muzzle | [verified: native `vr_weapon_calibration.c:2344–2470`; selector must explicitly supply seeded muzzle `(0,0,held_Z)`] |
| Native reload applies builtins before authored file and existing reset cancels adjustment | [verified: native `vr_weapon_calibration.c:2285/2559–2608`] |
| Primary Projectile Spawn Z is not used to adjust actual shot origins at this pin | [verified: primary cvar only read at `vr.c:6264` in conditional crosshair compensation, versus actual firing source owner `vr.c:4038` and `sv_phys.c:5475`; native source correction retained at calibration.c:2877/sv_phys.c:3956] |

Example: OSJC classic super shotgun requires held `(-6,0.3,7)` scale0.9 and
muzzle `(0,0,7)`, while stock uses `(-3.5,1,8.5)` scale0.8/muzzleZ8.5.
Native model preference and manual schema authorship do not preserve selecting
that built-in calibration. Keep native physical-muzzle crosshair instead of
reproducing primary conditional solo-only pointer/shot divergence; add no
global24-unit projectile correction.

## Proposed minimal adapter and open decisions

Reuse `vr_weapon_calibration.c/.h` and `menu.c`. Add one archived selector with
five names and range0..4, retaining useful primary enum order/name if convenient.
It changes classic calibration only, not native `r_enhancedmodels` or assets.
Copy literal preset data from the reference into existing schema-entry tables;
use existing slots/application/preflight and adjustment cancellation. No second
offset registry, generated profile file, model loader, renderer or reload loop.

Startup/game reload: native builtins plus selected contextual classic preset
before the authored file; authored calibration wins. Explicit live selection:
apply selected classic held/scale and seeded muzzle values without immediate
schema reload; retain enhanced geometry, source correction, melee metadata,
identities/roster and unrelated custom model calibrations. Do not clear all
slots or revive MP overlays. This intentionally improves preservation beyond
primary's broad classic rebuild at the existing per-model boundary.

Current lean: single cvar callback in calibration owner for console/menu edits,
with one existing Weapon Setup row; finite sanitize invalid values as Vanilla.
Registration must reuse native initialization; no callback until registered
slots exist. Rejected plain ReloadGame callback because schema undoes an
explicit preset. Rejected copying primary generated-cvar rebuild because it
duplicates ownership and drops useful slot metadata.

Preserve primary AD/Alkaline Plague branching and ordinary Enhanced-gamedir
selection, plus specialized QBJ3/Enyo/Dwell behavior. Reuse current contextual
fallback helpers/identities; do not add mod-name lists or new asset detection
frameworks. Exact family table composition, startup versus live precedence,
slot-capacity atomicity and implicit-muzzle provenance need the bounded source
disposition below before implementation. Keep MD5 fields and native selected
geometry untouched. Existing AD-family/root/alias coverage remains required;
do not silently shrink it to primary's older exact-directory check.

Expected at most350 net production lines including copied tables, public narrow
selector access and menu entry. Reopen before broader ownership/extra state or
limit overrun. Only one coding worker edits menu/calibration at a time.

## Review contract

Request local Astra source advisory on the verified brief and actual owners:
verify before critiquing, identify smallest complete table/precedence contract,
challenge unnecessary reset/state/policy duplication and preservation failures.
Depth budget the preset adapter; do not redo OpenXR/foveation, prediction,
global calibration writer, avatars or the four existing weapon sliders. Readonly,
no nested agents/builds/tests/probes/fixtures/benchmarks. Effective settings are
not exposed, so do not claim certified senior-skill/model settings.

## Final qualification

Only source/diff review now. After all goal implementation finishes, Linux/ARM
checks cover five exact tables, Enhanced→Vanilla held+muzzle, primary contextual
Plague aliases, startup authored override versus explicit live selection,
preserved MD5/source/melee/custom/wheel fields, cancellation, invalid selector,
capacity refusal without partial mutation and shared solo/MP consumers. Native
graphics/model preference remains unchanged. User live alignment/performance
and Windows builds are outside this pass. No runtime parity claim from tables.

## Adopted requested-Astra source disposition before implementation

Main spot-checked actual startup queues and application/seed/preflight owners.
The bounded advisory changed two load-bearing contracts; no certified effective
model/effort or senior-skill pass is claimed.

| Finding/recommendation | Main disposition |
| --- | --- |
| Unconditional startup authored-file precedence is false | Adopt precise primary sequence: each ReloadGame installs selected contextual defaults then authored file. A later changed selector, even from config, applies classic presets without reloading that file; unchanged string does not invoke native callback. Native SV_Init reloads before queued quake.rc; host_initialized cannot distinguish those config edits. No new config completion owner. This supersedes the unconditional startup language above. |
| Generic data cannot indiscriminately replace specialized game calibration | Adopt bounded context composition below; retain existing native contextual baseline and aliases. No new game-family registry, asset detector or wheel dependency. |
| Explicit seeded muzzle loses provenance | Adopt private preset mode in existing schema-application implementation. Public ApplySchema preserves existing behavior. After whole-batch preflight, preset mode resets targeted derived-muzzle X/Y/authored status and lets existing held-derived branch set Z/seed provenance. QBJ3 explicit muzzles, including zero, bypass reseeding. |
| Sequential table mutation is not atomic | Assemble all contextual overriding rows in one bounded batch and preflight before any calibration or adjustment change. On success cancel adjustment then apply through the same owner. On capacity refusal preserve calibration/session and restore prior accepted selector. No per-table apply loop or rollback store. |
| Callback registration/invalid values/recursion | Register selector once at end of Init after slot registration/initialized flag, then attach callback. Validate finite integral0..4 before indexing; invalid becomes Vanilla. Temporarily detach callback for normalization/refusal restoration; one accepted-selector integer is enough. No second calibration store. |
| Preserve independent fields/roster | Live writes target classic held/scale/muzzle only. Keep enhanced, source, melee, identity, custom untargeted values and wheel state. No Reset/ReloadGame in live callback, no MP state. |

Concrete required contextual batch:

- Generic Vanilla: existing16 stock rows; Enhanced: stock plus six changed OSJC
  rows. `enhanced` gamedir forces OSJC before other generic selections.
- Generic Authentic/Plague: stock plus literal changed rows producing complete
  corresponding16-row results, including mission-pack differences. Generic
  BlockQuake: eight rows only; live untargeted mission-pack/custom slots remain.
- AD: existing15 rows, Plague overrides seven ordinary guns plus plasma; other
  selectors retain AD defaults. Additional native AD-root games keep contextual
  Vanilla baseline, while other named generic choices target their root paths.
  Keep AD171 aliases outside the live batch. Do not reinterpret them as Plague.
- Alkaline: three fixed rows plus nine ordinary rows; Plague replaces those nine
  with inherited filenames, preserving fixed plasma. Retain LimJam's existing
  axe fallback without creating a new broader Alkaline classification.
- Enyo: existing ten fixed rows, independent of selector.
- QBJ3: copy26 fixed base held/scale and explicit muzzles from primary; no MP
  overlays. Dwell/dwellv2p2: copy13 unique fixed rows; duplicate v_axeb source
  initialization does not require another native slot. These missing tables
  also serve reload defaults before authored files, not just a no-op callback.

Write ownership stays calibration.c/.h and one Weapon Setup menu row; at most
350 net production lines including data/API glue. Plans/index/source checkpoint
must retain final software and broader inventory limits. Run no tests/builds
until all implementation is finished. Reopen before exceeding this boundary.

## Source implementation checkpoint

The adapter is source-integrated in calibration.c/.h and the existing Weapon
Setup page: five named presets, contextual tables, archived selector, whole-batch
preflight, private implicit-muzzle application and unchanged public schema API.
Reload applies selected defaults before authored files; live callbacks do not
reset/reload slots or change model assets. Shared solo/MP consumers remain.
The complete production delta is337 net lines, below the350-line boundary.

Main reviewed the full diff, schema/preflight/seed/registration owners, menu
bounds and actual primary tables. The requested-Astra read-only recheck found
one P2: generic BlockQuake also appended the Copper fallback, violating the
eight-row live preservation contract (even though primary appends that fallback).
Adopted correction: a private reload-defaults argument skips Copper and LimJam
fallback writes for a live effective BlockQuake selection, while reload retains
them. Forced Enhanced-gamedir and specialized context branches remain unchanged.
The same reviewer's bounded recheck closed that P2 with no new concrete blocker.
Independent enhanced/source/melee/identity/wheel and untargeted slot values are
preserved at the reviewed boundary. Effective reviewer settings are not exposed;
no formal model/skill certification or runtime parity is claimed.

Only source comparison and scoped whitespace checks were performed. Final
Linux/ARM software qualification and broader WPN/profile acceptance remain open.

The subsequent [reusable AD contextual preset](ad-weapon-preset-2.0-plan.md)
extends the selector to0–5 through the same schema adapter and native menu row.
It retains this plan's original choices, fixed-context and enhanced-directory
precedence, implicit muzzle handling and live/reload distinction. The new
explicit AD selection makes the existing table available in generic contexts;
it is not automatic asset/family detection. Its own before-code disposition,
production commits and final source-only review limits are recorded there.
