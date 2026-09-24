# Controller weapon adjustment on the vkQuake 2.0 client

## Decision and scale

Restore the inherited `vradjustweapon` and `vradjustmuzzle` behavior with the
existing 2.0 calibration slots and OpenXR input/view owners. One held and one
muzzle offset per weapon should serve solo and multiplayer when proven against
QBJ3/Enyo; their current MP overlay remains until that proof. This is a solo
engine port, so avoid a new weapon registry, input system or renderer.

## Verified environment facts

| Fact | Evidence |
| --- | --- |
| Source freezes the active viewmodel at the captured controller pose, tracks live controller movement, consumes dominant trigger to commit a recentered held or muzzle offset, and saves through `vr_weapons.txt`. | [verified: pinned source `Quake/vr.c:7590-7960`] |
| Source refuses controller adjustment for controller-centered immersive melee/paired akimbo paths where ordinary held offsets do not apply. | [verified: pinned source `Quake/vr.c:7590-7655`] |
| 2.0 has one calibration slot owner with held, muzzle, enhanced and MP fields; both Vulkan viewmodel and private firing command already query it. | [verified: `Quake/vr_weapon_calibration.c:628-731`; `Quake/r_alias.c:661-717`; `Quake/vr_input.c:3458-3565`] |
| 2.0's current tracked presentation pose is set on `cl.viewent` in `V_UpdateTrackedViewmodel`, and gameplay command pose is prepared separately. | [verified: `Quake/view.c:461-502,1510-1537`; `Quake/vr_input.c:3458-3565`] |
| Dominant trigger already has hysteresis and one input owner. Gameplay bindings are published from `VR_InputCommands`; focus/context loss gates and releases inputs. | [verified: `Quake/vr_input.c:3107-3180,3275-3395`] |
| Local model offset/world and inverse math exists in the locomotion module but is currently private to that file. | [verified: `Quake/vr_locomotion.c:166-227` and header] |
| The effective MP overlay can be nonzero and is not yet proven redundant on QBJ3/Enyo. | [verified: `docs/migration-weapon-calibration-unification.md`] |
| Classic calibration schema persistence and `vrweaponsave` are available in the existing owner. | [verified: `Quake/vr_weapon_calibration.c`, `VR_WeaponCalibrationSave`] |

## Proposed smallest adapter

Keep adjustment session state in the existing calibration owner. Capture model
identity/format, model origin correction, dominant physical hand, viewmodel
world anchor and hand rotation when a command starts. On the main-thread view
setup, override only the *presentation* viewmodel pose with the frozen copy;
continue updating live controller pose for the recenter calculation. Consume a
new dominant trigger press in the existing input owner before it can become an
attack binding. Cancel on focus/reference/hand/model/profile/map/context loss;
release any previously emitted trigger key. Commit through the current slot
using the existing model↔world math, subtracting the still-authoritative MP
overlay before changing a base value. Save through the same schema owner after
a successful commit; a separate save command can support manual cvar edits.
Avoid frozen presentation state leaking into network command pose, collision,
melee contact or desktop rendering. One main-thread session owns the captured
state; frame workers read a prepared immutable pose.

The first vertical proof is a stock shotgun in a disposable local Linux OpenXR
session: begin adjustment, observe a stationary model while moving the dominant
controller, press trigger and confirm no shot/command attack, see one changed
offset, reload the game and see that offset retained. Then prove muzzle recenter,
left-handed mapping, enhanced model distinction and MP overlay arithmetic with
focused code checks. QBJ3/Enyo mode parity remains a separate final gate.

## Alternatives and open decisions for Astra

1. **Session in calibration owner, hooks in existing view/input owners (lean).**
   Retains one calibration registry and the current OpenXR and Vulkan owners.
2. **Entire adjustment in `view.c` or `vr_input.c`.** Reject: would duplicate
   calibration policy or split frozen pose from persisted model identity.
3. **A second calibration UI pipeline/model manager.** Reject: duplicates
   rendering, model lifetime and task policy merely to freeze a viewmodel.
4. **Only expose cvar sliders.** Useful fallback but fails inherited
   move-controller-to-grip/muzzle behavior; not parity.

Audit the capture/commit frame boundary, left-handed transforms, model format
and scale-origin assumptions, trigger/key ownership under focus loss, and whether
the proposed view pose hook can stay narrow. Identify what can be deleted or
reused and the smallest code proof. Do not review the complete 185-item map,
OpenXR lifetime, foveation, server netcode, or the full calibration unification
decision. No human preference decision is expected unless a genuine physical
behavior tradeoff remains after the evidence check.

## Astra senior-review disposition

The read-only review ran as `gpt-6-astra` at `xhigh`, verified from the local
agent turn metadata. Main spot-checked the collision call order, trigger-held
mapping, model transform scale and source return-to-grip state. The review
changed the design, rather than merely endorsing it.

| Finding | Decision |
| --- | --- |
| Freezing `cl.viewent` alone still lets live collision correction move it and leaves the crosshair on a live pose. | **Adopt.** Capture the collision-free presentation grip, bypass presentation collision during adjustment, and place the calibration muzzle cue from that frozen pose. Keep command/contact poses live and separate. |
| One consumed trigger edge can turn into a shot on the next held sample; gating the whole hand also suppresses movement and command-pose admission. | **Adopt.** The input owner uses a trigger-only suppression latch through release, including begin-while-held, commit, cancellation and save failure. Retain normal full-hand gating only for actual focus/context loss. |
| Held recentering can subtract frozen/live grip displacement through the existing inverse transform; header origin and live rotation need not be copied into a second state. | **Adopt.** Expose the narrow existing model-space inverse and keep captured rotation, format, handedness and scale identity. Use the distinct muzzle inverse for muzzle recentering. |
| One calibration transaction and existing view/input/render task owners suffice. | **Adopt.** Mutation stays on the main thread; the view hook prepares a frozen pose before task submission and does not introduce a second render snapshot. Save result is a separate persistence boundary. |
| The donor retains a frozen post-muzzle hold until the controller returns within eight Quake units. | **Adopt.** The user's parity requirement decides this; no new preference question is needed. Immediate live resumption would be a behavior regression. |

The first proof must include stock-shotgun grip and muzzle adjustment with
tasks enabled, head/wrist movement and a nearby wall, begin-while-held and
release/repress trigger behavior, cancellation, and save/reload. Add a
left-handed rolled/pitched enhanced-model case and MP overlay arithmetic before
claiming broader parity. These remain implementation and integration gates.

## Implementation checkpoint

Controller adjustment now supports classic alias grip and muzzle recentering
through `vradjustweapon` and `vradjustmuzzle`. It captures a collision-free
presentation pose, freezes only that pose, suppresses the dominant trigger
until release, preserves the MP overlay when editing the shared base offset,
and saves through the existing schema owner. The muzzle path shows the frozen
target through the existing stereo cue and retains the frozen pose until the
live grip returns within eight Quake units. The local Linux build links.
Enhanced MD5 profile persistence and hardware behavior checks remain.
