# Peril roster and desktop weapon wheel

2026-10-04. Goal: retain all Peril weapon calibration and represent its actual
native weapon inventory correctly, while making the existing desktop wheel
available through a Q binding with exclusive mouse selection.

## Verified baseline

- Installed assets are in quakespasm_straight/peril3.0. The earlier inventory in
  peril-vr-weapons-2.0-plan.md covers twenty packaged viewmodels, twelve active
  model identities, effective pak2 QC, and the existing paired SMG adapter.
- vr_weapon_calibration.c already contains twenty geometry-derived Peril held
  and muzzle presets, with unchanged copied stock triples yielding to these
  defaults. Authored changed or partial overrides remain authoritative.
- vr_weapon_menu.c owns one shared catalog, live ownership/selectability,
  optional wwheel/schema precedence, prepared render snapshot and release.
  Its built-in profiles do not currently include an explicit Peril roster.
- Desktop wheel rendering already uses CANVAS_DEFAULT with a screen-space
  overlay and an absolute SDL mouse pointer. VR uses its separate existing
  model/panel presentation. Reuse these paths.
- cl_input.c already registers +vr_weaponmenu/-vr_weaponmenu and temporarily
  releases desktop mouse capture. menu.c already lists Weapon Wheel in its
  default bindings. Misc/vq_pak/default.cfg is the packaged default script.
- IN_MouseMotion and IN_MouseMove lack an explicit desktop wheel guard.
  Cancellation and relative capture restoration need auditing against focus,
  destination changes, multiple binding sources and re-entrancy.

## Implementation

1. Read effective Peril QC source and PAK models, verify selectors/impulses,
   ownership/stat masks, upgrade replacement and ammo. Add the smallest roster
   adapter in the existing profile owner. Do not invent weapons from dormant
   files, approximate inventory bits, duplicate catalogs or add wire channels.
   Keep wwheel and explicit schema authoring authoritative. Audit the existing
   twenty calibrated models rather than replacing working offsets.
2. Bind Q to +vr_weaponmenu in the existing default script. Retain user config
   and explicit unbind precedence. Keep the existing controls-menu entry usable
   even with a supplied mod bindlist.
3. Make the existing desktop wheel own mouse motion/buttons while open: no
   camera/movement accumulation or accidental mouse attack. Restore normal
   capture after release/cancel using the current key/input ownership. Keep
   keyboard movement and VR input semantics unless a demonstrated incompatibility
   requires a narrow change. Selection remains absolute mouse only and display
   remains screen space; no new UI/input state machine or renderer.
4. Review both slices together, obtain Astra xhigh review on native roster and
   input lifecycle, address material findings, then run meaningful Linux software
   checks with actual Peril assets/QC and desktop input. Physical VR grip comfort
   remains user qualification. No intermediate builds/test runs.

Minimal adapter versus rewrite: the catalog, SDL pointer, canvas, button commands,
render tasks and input capture already exist. A second desktop wheel would
reimplement selection, inventory, rendering and focus state without evidence
of incompatibility. Only missing profile/default/ownership seams are in scope.

End-to-end proof: hold Q in desktop Peril, move the mouse over the displayed
owned weapon, release and observe the actual QC-selected weapon. Mouse motion
must not change camera angles; focus/console/menu cancellation must not select
or leave mouse capture stuck. Verify complete Peril roster, upgrade transitions,
empty ammo and calibration override precedence using installed assets read-only.

## Astra senior review disposition

Astra (`gpt-6-astra`, xhigh), verified actual source and installed Peril assets
before critique. Review is read-only; integration and qualification remain main's
responsibility.

| Finding | Disposition | Reason / smallest correction |
| --- | --- | --- |
| Keep shared catalog, render snapshot, SDL pointer and command owner | Accepted | Missing profile and capture seams do not justify a second wheel or renderer. |
| Prepared upgrade labels/icons can disagree with hit boxes | Fixed | Resolve from supplied stats in existing visible descriptor; copy labels into existing frame storage; draw consumes snapshot. |
| Held binding is not necessarily a pending opening; canceled/old key presses can survive queued commands | Fixed, final source review accepted | Add press lifetime metadata only to the existing desktop wheel key-command boundary; clear on cancellation and preserve native VR dispatch. |
| Authored held model can retain unrelated built-in upgrade label | Fixed | Resolve Peril label from effective held identity; reuse existing schema label formatter for unknown authored models. |
| Dedicated QC proof must not call public SDL-sampling release | Fixed | Use ReleaseCatalog for explicitly supplied pointer; connected graphical fixture separately checks actual SDL pointer and public release. |
| Supplied WM/PG images could be reused | Deferred | VR already previews the correct native pickup model. Desktop uses verified stock category art or accurate labels; exact SMG artwork is absent. No new image loader or invented art is required for correct identity. |

The host GCC 16 build also exposed a missing declaration for the new queued-key
check. Shared helper contracts now live in input.h/keys.h and generated commands carry validated press tokens. The now-unused key-state
getter was removed. An unrelated pre-existing co-op borrowed-policy
warning is retained as a warning in this local qualification build; this change
does not alter that server code. Portable release builds retain their existing
compiler and warning configuration.

A further senior review caught an accepted source stranded by a rapid queued
release/re-tap/release. Rejecting stale selection must still retire the existing
released kbutton source, retaining another held source. The correction belongs
in that existing latch, with no second accepted-source or input state machine.

Final Astra xhigh source recheck found no remaining material findings in these
bounded changes. Active-wheel closure now reuses one private helper; full modal,
focus and rebind cancellation separately invalidates queued command lifetimes.
Retiring a released Q does not invalidate a fresh independently queued R. This
was an explicit architecture recheck after the edge cases: preserve existing
kbutton and wheel state, with command metadata only, rather than add a parallel
accepted-source state machine. Final native fixture qualification remains main's
responsibility.
