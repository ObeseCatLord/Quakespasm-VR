# Desktop weapon wheel input (2.0)

## Findings and implementation plan (before edits, 2026-10-04)

The behavioral reference is the existing `+vr_weaponmenu`/`-vr_weaponmenu`
command pair in cl_input.c, the absolute `IN_GetMousePos` pointer and
`CANVAS_DEFAULT` overlay in vr_weapon_menu.c, and the ordinary key-button
source tracking in keys.c. The shared catalog/render/session owner stays intact.

Observed gaps:

- IN_MouseMotion accepts relative deltas while the wheel is open and
  IN_MouseMove can apply deltas accumulated before opening. IN_Deactivate does
  not clear these accumulators; IN_ClearStates is empty.
- SDL2/SDL3 discard relative motion while deactivated, but continue delivering
  mouse buttons and scroll to Key_Event. Suppression belongs in this shared
  key seam, preserving matching releases and mouse-bound wheel openers.
  Key events are queued before Cbuf_Execute, so a Q press and click in the same
  pump must also respect pending wheel ownership.
- Renderer/session cancellation does not clear the command's held-source latch
  or restore capture. Console/menu transitions deactivate before changing
  key_dest; focus loss currently cancels only renderer state. Reconcile desktop
  ownership from IN_UpdateInputMode and immediately on destination/focus/input
  clear seams. Keep stereo focus behavior unchanged.
- Existing default controls entries are retained with custom bindlist entries,
  but a duplicate custom wheel command can shadow the built-in label. Move the
  wheel entry into the protected standard-binding section.
- Packaged default.cfg lacks desktop Q. Add it only there; do not migrate or
  repair saved binds, and preserve explicit unbinds. This is the only authorized
  source-config change; no installed assets/configs will be modified.

Minimal change: reuse the existing command latch/capture flag, add desktop-only
cancel/reconcile helpers, clear relative deltas at both capture transitions and
motion guards, and consume wheel-owned mouse downs in Key_Event. Release mouse
button gameplay commands already held at opening, without clearing keyboard
movement or VR/controller commands. Preserve two-source wheel hold/release.
No new renderer, selection model, catalog, or general input state machine.

Verification is deferred until implementation is complete. Add production-code
fixtures and a Linux live desktop GDB script covering absolute hover/release,
no camera/mouse-movement changes, mouse command routing, multiple sources,
rebind/unbind, focus/console/menu/game-context cancellation and capture restore.
Live checks require a debug build and isolated write root/assets supplied by the
integration owner. Neither build/deploy nor physical VR qualification is part of
this worker's initial source audit.

## Implemented input seams

Production edits are limited to cl_input.c, in_sdl.c, keys.c, menu.c and the
packaged Misc/vq_pak/default.cfg. No changes to vr_weapon_menu.c/.h,
gl_screen.c, gl_vidsdl.c, installed assets or saved configs.

- Desktop command cancellation clears both held-source slots and the latch,
  discards relative deltas and restores capture only in a focused game input
  destination. IN_UpdateInputMode and mouse consumption reconcile external
  wheel/session cancellation. Incomplete signon cancels selection, while capture
  may remain active in the focused game destination (ordinary loading behavior).
- Console/menu entry cancels immediately before its existing deactivation;
  Key_ClearStates cancels before synthetic releases. A menu that cancels desktop
  wheel ownership releases relative capture even in fullscreen with ui_mouse 0. Desktop focus loss clears
  held keys and deactivates capture, and focus gain restores it only for game
  input. Mirror focus handling in stereo retains its existing behavior.
- Both accumulation and consumption guard desktop wheels. Deactivation clears
  pre-open motion; restoration clears wheel motion. IN_Activate and
  IN_HideCursor cannot steal an open desktop wheel's absolute cursor.
- Key_Event consumes new mouse gameplay/scroll presses while the wheel owns the
  mouse, including an opening bind queued earlier in the same SDL pump. Their
  unmatched releases remain stray events and cannot execute another binding.
  Existing mouse holds receive releases; mouse-owned pending attack edges are
  removed at opening while keyboard/controller attack contributors stay held.
  Exact +vr_weaponmenu mouse bindings remain usable as hold/release sources.
- Two-source release uses the existing kbutton machinery. Queued desktop opens
  whose command token was canceled or superseded before Cbuf executes are rejected. Very short
  press/release taps completed within one SDL pump therefore do not open/select.
  This check applies only to desktop, preserving VR command dispatch behavior.
- The existing Weapon Wheel row now sits before the custom-bindlist section, so
  standard-entry filtering preserves its label and one usable native bind row.

## Defaults and saved-config/reset lifecycle

Q is assigned exclusively by the existing packaged default.cfg. There is no
startup fallback or migration state. Fresh installs and the existing controls
reset (`exec default.cfg`) receive `bind q +vr_weaponmenu`. Later saved-config or
autoexec commands remain authoritative, including `bind q +jump`, `unbind q`,
and `unbindall`. An old config containing unbindall without Q will require reset
or manual `bind q +vr_weaponmenu` / assignment using the Weapon Wheel menu row.
Reset also resets the other controls through the existing default script; it
is not a Q-only migration.

## Deferred qualification fixture

`tests/desktop_weapon_wheel_input.gdb` uses initialized native Key_Event/Cbuf,
SDL_PushEvent (SDL2/SDL3), IN_MouseMotion/IN_MouseMove, the actual controls menu,
the existing renderer and real server QuakeC. It covers pre-held mouse attack,
Q and mouse in one SDL pump, exclusive button/scroll ownership, suppressed
mouseup after cancel/rebind, keyboard movement, keyboard/mouse two-source wheel
holds, cursor reacquisition, post-release deltas, native cancellation and
config/reset precedence. It captures screenshots of native hover and the
selected weapon, then requires a real QC active-weapon transition.

Run only after the integration owner builds the final sources, with a private
X11 display and a fresh isolated basedir containing stock id1 assets plus the
updated packaged defaults. Set QSVR_WHEEL_PRIVATE_DISPLAY=1,
QSVR_WHEEL_BASEDIR and QSVR_WHEEL_RESULT, and run GDB from the repository root
with an outer `timeout 120s`. The script kills only its inferior to avoid exit
config writes. No physical input or foreground focus request is issued. A private Xvfb
server owns the virtual pointer. XTest moves it to the inferior PID window,
and SDL's actual absolute pointer must match it. SDL3 ignored synthetic
XSendEvent motion during qualification, so the fixture now requires this private
virtual-pointer path. It fails rather than substitute manufactured hover state.

Focus changes are bounded native input-seam calls on the isolated display;
incomplete signon is an explicit lifecycle fault injection, not a network
handshake test. Screenshots must still be inspected by main. This stock input
fixture does not qualify Peril inventory/selectors or physical VR behavior;
those belong to the roster/integration checks. A private-display launch is
required because ordinary SDL window startup/capture can affect a real desktop.

Verification performed by this worker: read-only source/lifecycle inspection
and `git diff --check` on owned production files (passed).
No test, build, live fixture, deploy or commit was run; main owns final execution.

## Interface handoff to main

No new renderer API is required for the implemented behavior. Existing
VR_WeaponMenu_Cancel/IsOpen/IsOpenVR and the command lifecycle suffice. Session
invalidation by the renderer/catalog owner is reconciled on the next input-mode
or mouse-consumption call. IsOpen currently reports raw open state rather than
session validity; preserve the existing owner-driven session invalidation and
check a map/renderer cancellation in final integration. Do not introduce a
second session validator in input code merely to remove that one-frame seam.

## Astra P2 follow-up plan (before edits)

A canceled Q that remains physically held must not count as a pending open or
suppress gameplay mouse input. Physical keydown alone also cannot distinguish
an old queued press/release from a fresh Q press following focus loss/gain.
Use per-key command lifetime metadata at Key_Event/Cbuf dispatch: an unsigned
press token (global issuer skips zero) plus an actual pending-down flag. Tag only
exact desktop wheel bindings; preserve manual untagged and ordinary VR commands.
Cancellation invalidates all desktop command tokens even before capture exists.
Validation consumes a pending down, validates releases after physical keyup, and
rejects stale tokens without touching newer tokens. Rebinding a held desktop wheel
cancels instead of queuing a selecting release. No new selection/render state.
Final runtime remains main-owned; extend native fixture before handing back.


## Astra P2 implemented handoff

keys.c now issues a nonzero unsigned token for each exact desktop wheel binding
press actually dispatched to Cbuf. Per-key pending flags identify only queued
opens; consuming the down clears pending, while physical up queues the same
press token and clears pending. Tagged commands validate at execution time,
even if stereo mode changed; mismatches cannot touch a newer token. Invalid
matching downs retire their token. Releases validate after physical keyup and
retire only their matching token, preserving independently queued fresh sources.
Manual untagged commands and ordinary stereo dispatch keep their existing path.

IN_CancelDesktopWeaponMenu invalidates queued metadata before checking capture,
so modal/focus cancellation also works before the opening command executes.
Cancellation leaves a still-held physical Q unable to claim pending mouse
ownership. Rebinding/unbinding a held or queued desktop wheel source cancels the
wheel and queued tokens instead of allowing the old release to select; unrelated
button rebinding uses the existing release behavior. The source latch and renderer
remain the only actual wheel session owners.

Fixture additions cover canceled-but-held Q allowing native mouse attack,
pending-only cancellation, old queued Q down/up followed by focus loss/gain and
a fresh Q down before Cbuf, one fresh opening with unchanged impulse, valid
fresh-token release, and no selection when a held source is rebound.

Optional QSVR_WHEEL_MOD=peril3.0 and QSVR_WHEEL_MAP=start launch the same fixture
against connected desktop Peril in the integration owner's isolated asset base.
Default launch remains stock e1m1. No Peril inventory is seeded: current owned
inactive selectable inventory drives the hover/release/QC assertion. The fixture
fails explicitly if that map has no such weapon. Full eight-owned-slot connected
qualification is not claimed; main must provide a verified inventory seed or
its own acquisition/roster qualification. No Peril code, runtime scripts, plan,
menu/default source, or unrelated sv_phys warning was changed by this follow-up.

Follow-up checks: source inspection only until production/fixture edits stabilize;
main retains build/runtime ownership. No physical cursor or foreground focus
operation was added.

After follow-up stabilization, owned-source `git diff --check` passed and the
fixture's embedded Python parsed successfully with `ast.parse` (not executed).
No build, native test, runtime probe, deployment, or commit was run. Follow-up
write ownership is released to main for final review/build/runtime integration.


## Final Astra accepted-hold retirement fix

An accepted Q source can outlive its token if Q up/down/up is fully queued
before Cbuf runs. Rejected tagged downs now obtain a bounds-validated physical
release result from the existing key-command validator. When that key is
released and the existing desktop capture owner is active, cl_input uses the
existing KeyUp to retire only that accepted source. If no source remains, the
existing cancellation path closes/restores capture without selecting. An
independent accepted R remains held and releases normally; a currently held
fresh press cannot retire its older accepted source. No accepted-token array,
selection state or renderer API was added. The validator's optional release
output replaces any need for a general Key_IsDown getter.

The native fixture adds accepted Q -> queued up/down/up cancellation with no
impulse or reopening, and accepted Q+R -> queued Q up/down/up leaving exactly R
held, followed by its normal final release. Production/fixture edits are stable;
main retains runtime/build ownership. This follow-up touched cl_input.c,
keys.c/.h (existing validator signature only), the desktop fixture and this doc.

## Final senior review and fixture adjustments

Final Astra xhigh source review accepts active-wheel closure separated from
global command invalidation. Rejected Q taps retire the existing released
kbutton source without selecting, preserve an accepted R and preserve a fresh
independently queued R. Focus/menu/rebind cancellation still invalidates all
queued lifetimes. No additional accepted-source state machine was added.

The live fixture naturally waits for the spawn setangle lock and Quake's initial
discarded movement packets to expire. It does not change these input policies.
A debug binary is required to expose static menu helpers; optimized shipping
builds are qualified separately. The paired/peril runtime fixtures use distinct
GDB convenience variable names (a variable named `$ss` is an x86 register, not
a private pointer). Qualification outcomes are in peril-desktop-wheel-2.0-results.md.
