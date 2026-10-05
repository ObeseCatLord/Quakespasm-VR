# Peril calibration/roster and desktop wheel qualification

2026-10-04. Implementation reuses the existing calibration, shared wheel
catalog, render snapshot, SDL mouse and kbutton/command owners. Astra xhigh
review found no remaining material source findings after the corrections in
[the plan/disposition](peril-desktop-wheel-2.0-plan.md).

## Passed behavior

- ASan/UBSan production-code fixtures with installed Peril assets read-only:
  all twenty calibrated held/muzzle presets, unchanged copied stock triples,
  changed/partial authored overrides, stock isolation and independent paired
  MDL geometry/skin/animation splitting oracle.
- Peril wheel policy: eight selectors, twelve active model identities,
  native labels/held/pickup paths, upgrades and inverse, grapple priority,
  parent ownership, ammo gates, schema/wwheel precedence, prepared labels/icons
  surviving mutable stats and authored held-identity label merging.
- Actual effective Peril QC: all eight wheel releases select the correct held
  model; Shadow Axe/grapple/Widowmaker/Plasma replacements and inverse,
  two-unit SSG/SNG thresholds, two-shell Widowmaker and explicit authored
  selector/command/path/ammo overrides. Existing server stat production, FTE
  encoding and real client parsing agree on items, modifiers, equipped model,
  ammo and capacities. This stat test is an in-process wire roundtrip.
- Native paired SMG regression: sixteen VR and sixteen desktop nailgun shots,
  eight desktop super-nailgun shots, native press/hold/release/repress/exhaustion,
  cadence, alternating physical muzzles, rejected/stale/invalid/nested poses,
  body/basis restoration, real QC relocation, BSP clamp and target correction.
- Graphical desktop stock e1m1: 49 recorded cases; Peril start: 50. Fresh Q
  default, reset/remap/unbind precedence, one usable Controls row with mod
  bindlists, no relative camera/movement leak, mouse-button/scroll ownership,
  held keyboard movement, independent wheel sources, focus/console/menu/renderer
  cancellation, rebind, disconnect and all reviewed queued press orderings.
- Private Xvfb/XTest virtual pointer -> actual SDL absolute sample -> native
  visible hit box -> public wheel release -> normal client/server command
  delivery -> actual QC weapon change. Both graphical checks select the axe
  and confirm the real server and client equipped selector, not just a queued
  impulse. Screenshots of stock/Peril wheel and post-selection were inspected.
  Peril displays all eight native owned slots after the existing
  `sv_giveall all` admin command; no pickup-acquisition claim is made.

## Qualification boundaries

The graphical test uses a private CPU Vulkan driver and virtual X11 display.
No physical cursor/focus, NVIDIA driver, installed game assets/configs, audio
device or headset is modified. Actual mouse samples and real QC are used; the
fixture does not manufacture rendered hover state. A debug build exposes static
menu helpers for GDB. Optimized native redistribution builds are checked
separately. Host GCC 16 diagnostics in unrelated existing code required a local
debug warning-policy adjustment; no production compiler policy changed.

The initial harness failures were retained privately: a short-lived private X
server, optimized helpers absent from debug symbols, an x86 `$ss` register name
used as a convenience variable, spawn setangle/initial discarded packets and
SDL3 ignoring XSendEvent absolute motion. These were harness corrections, not
reasons to change gameplay/network/renderer behavior. Peril start initially owns
no inactive selectable weapon; native give-all supplies the explicit roster
case. All final qualification cases pass.

Physical VR grip comfort, eye tracking, headset input/graphics and performance
measurement remain user testing. VR model preview/controller paths are reused.
Desktop uses verified stock category icons or accurate native labels where no
matching icon exists; it does not invent Peril SMG/replacement artwork.

Existing saved bindings/explicit unbinds remain authoritative. New defaults and
Controls Reset bind Q; an older saved config can assign Weapon Wheel in
Customize Controls or use `bind q +vr_weaponmenu`.
