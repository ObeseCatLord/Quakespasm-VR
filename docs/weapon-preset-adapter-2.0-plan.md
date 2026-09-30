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
