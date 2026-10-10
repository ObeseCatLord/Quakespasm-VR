# Desktop gamepad behavior and independent XR controls

## Goal and references

Desktop gamepads should retain Ironwail/vkQuake shoulders for weapon cycling,
left trigger for jump, right trigger for attack, with a default hold binding for
the weapon wheel and easy stick selection. Native gyro is opt-in. Standard SDL
controller and keyboard/mouse events remain the Steam Input integration boundary.
No new Steam Input API, controller driver, config registry or polling service.

## Evidence and minimal adaptation

Packaged baseline bindings match both donors. Saved XR-generated physical
button assignments explain the desktop divergence. Gyro defaults match Ironwail:
enabled with a hold-to-disable mode, without a packaged clutch binding.
SDL3 opens the sensor; the existing native gyro application gate controls aiming.

Append dedicated XR button keycodes, keeping every existing numeric code,
and enlarge the existing key table to 512. Reuse native binding/capture/config,
press/release ownership and XR context gates. Keep ordinary menu/modal navigation
codes. Resolve gameplay/capture keys before XR ownership and binding-dependent
melee/wheel checks. Remove capture's automatic command-wide binding eviction;
explicit clear-all remains available with a clear label and discoverable extra
assignments. Serialization-provenance-only or swapping binding arrays cannot
solve simultaneous physical/XR key state collisions and would add policy/state.

Use the existing desktop catalog, one-ring layout and prepared slot centers.
Publish swap-aware selection axes during native controller polling before queued
release executes. One wheel-session owner reconciles mouse and stick changes in
frame preparation and release; an unchanged input cannot repeatedly steal focus.
Neutral retains a stable weapon choice. Select weapons by angular proximity above
a deadzone, without the mouse annulus's 16-slot cutoff. Do not angular-select
quicksave/load/co-op actions. Actual mouse retains access to those actions.

Use look axes by default; an option permits move axes for single-stick layouts.
Suppress stick look, gyro and pending flick behavior while the wheel is open.
Preserve movement with look-axis selection; consume it for move-axis selection.
Unplug cancels before releasing only the physical controller's contributions.
Reuse existing session/token/focus cancellation; no new generation protocol.

Default physical RTHUMB opens the wheel. Preserve donor trigger/shoulder defaults;
add normal face-button controls and an explicit controller-default restore owner.
Gyro modes stay configurable with descriptive labels. Repair only relevant
installed controller/gyro preferences once with a backup; do not wipe profiles,
rewrite settings each startup, infer historical intent, or touch VR_ALTFIRE hooks.
Existing dedicated/custom XR assignments remain authoritative.

## Astra disposition

Verified local gpt-6-astra at xhigh; source-backed read-only review.

| Recommendation | Disposition |
| --- | --- |
| Dedicated XR keys required for runtime and saved separation | Adopt; same native key table/event owner, stable existing numbers. |
| Capture must not evict mouse/SDL/XR assignments | Adopt; replace captured key only; show additional assignments; explicit clear-all label. |
| Refresh stick choice before command execution, frame preparation and release | Adopt one existing-wheel source-aware owner; no second selector/session. |
| Absolute mouse change, not filtered relative events, owns mouse takeover | Adopt; deterministic simultaneous-input priority and neutral retention. |
| Desktop has one ring; avoid new multi-ring policy | Adopt; use existing prepared slot geometry. |
| Unplug cancellation must not clear unrelated keyboard/XR holds | Adopt existing cancellation before retiring physical keys/caches. |
| Replace literal 256 keydown declarations and key count tests | Adopt; audit hard-coded capacities and preserve old numeric IDs. |
| Gyro off, Ironwail baseline, explicit reset and relevant preference repair | Adopt; no recurring automatic migration/overwrites. |

## End verification

After implementation: strict native build; native SDL virtual controller input,
real Key_Event/Cbuf/gyro/wheel owners; current owned weapon selection on same-frame
axis change and release; camera unchanged; neutral and source takeover; swapped
and selected axes; unplug/focus cancellation; native mouse wheel regression;
mouse/SDL/XR bindings coexist after capture/save/reload; hook bindings preserved.
Private profiles/Xvfb avoid physical input and installed config changes in tests.
Physical Steam Controller/Steam Input hardware mapping remains user validation.
Cross-platform release builds and automated deployment/publication follow once
implementation and relevant native checks pass.


## Final Astra review disposition

| Finding | Disposition |
| --- | --- |
| Unplug erased a surviving keyboard wheel token | Adopted: invalidate only native SDL/ALT tokens, including pending native opens. |
| Delayed mouse-to-stick takeover swallowed the first deliberate deflection | Adopted: consume mouse activity every poll. |
| Neutral/deadzone noise dismissed mouse action hover | Adopted: only finite deflections above deadzone acquire stick ownership. |
| Back/Start/menu-arrow aliases lacked unplug releases | Adapted: record the actual emitted identity per existing SDL button/axis source; merge the six shared navigation aliases at the existing native key owner. Independent ordinary input survives native release. |
| Camera could turn on the same poll that equips a wheel choice | Adopted: retain wheel camera suppression through that poll, reset by the next native poll. |
| Menu arrow ownership survived menu/game destination changes | Adopted: poll existing emulated sources every frame; use actual emitted ownership for old state and destination-gated axes for desired state. |
| Binding capture must retire the old shared-alias press | Adopted: explicit capture retirement at native key owner before installing the replacement binding. |

The current single-stick Steam Controller installation uses movement axes for
selection; the generic gamepad default uses look axes. Both reuse the same
selector and the menu setting. Existing settings were backed up locally, with
only relevant controller bindings, gyro enable and wheel-axis preference changed.


## Qualification result

Strict native Debug build passed. The SDL3 virtual-gamepad proof passed using
private Xvfb/XTest and a fresh profile: gyro off with a real sensor sample,
normal stick look, directional wheel selection, real QuakeC equip, neutral axes,
Back/Tab release, mouse ownership and deadzone rejection, delayed mouse-to-stick
takeover, same-poll axes plus release with camera suppression, Move/swapped Look
routing, and keyboard wheel/scoreboard holds surviving native unplug until their
own releases. No hover, inventory, key-state, or equip result was manufactured.

The native mouse-wheel lifecycle regression passed. Native XR action-frame
lifecycle/gameplay probes passed, including authoritative movement and ammo use;
these use completed input-frame injection, not headset hardware. Native custom
binding capture/save/reload passed with and without post-config overrides,
including mouse/SDL/XR attack coexistence and Sacrilege hook aliases. ASan/UBSan
adapter, default-binding and movement-continuity fixtures and stable key-name
roundtrips passed. Physical Steam Controller and Steam Input profile validation
remain with the user.
