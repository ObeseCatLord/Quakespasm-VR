# Weapon wheel controller defaults

The existing input bridge already maps ordinary dominant stick-click (Vive:
pad-click) to RTHUMB. Index reserves stick-click for alternate fire and exposes
pad-touch in the completed OpenXR frame. Reuse RTHUMB for dominant Index
pad-touch, only in gameplay or binding capture, instead of adding another
key namespace or querying OpenXR from the input owner. Left-handed role mapping,
key aggregation, release dispatch and all existing OpenXR action bindings remain.

Default RTHUMB becomes +vr_weaponmenu; jump remains on the offhand trigger.
Remove the old right-stick-up wheel default. The existing fill-only default owner remains unchanged: it does not repeatedly
rewrite saved/custom bindings. For the user's existing installations, deployment
updates inherited RTHUMB +jump and stick-up +vr_weaponmenu entries and removes
exact vr_turn180 bindings, on any key. The optional command remains available
for deliberate future binding. There is no default 180-turn binding.

Pad touch needs a narrow release gate after focus/context/profile changes, so a
finger left resting on the pad cannot reopen the wheel on resume. This gate
must not block stick locomotion or snap/smooth turning. Reuse the existing hand
lifetime with one pad-touch flag; do not add another state machine. Normal
menus ignore touch unless capturing a binding. Vive's legacy click-to-cycle
impulse must not run when that click opens the weapon wheel.

At completion, run the existing native default/input fixtures with added touch
press/release, held-touch recovery, role and custom-binding checks. Then rebuild
all three native packages. Deployment changes only the requested existing wheel
and turn bindings in the active global/mod configs, so already configured users
get the new defaults without resetting unrelated settings.

## Astra disposition

Astra xhigh review (effective settings verified locally) caught two interactions.

| Recommendation | Disposition |
| --- | --- |
| Exclude dominant Index pad axes from turning/neutrality when wheel-bound | Adopted: filter only copied native input samples, retaining raw XR frame and all stick/other-profile axes. |
| Do not continually overwrite deliberate later bindings | Adapted by deletion: remove engine migration entirely; update requested saved defaults only during this deployment, retain fill-only default owner. |
| Keep one touch-release flag in existing hand lifetime | Adopted. |
| Validate off-center held touch, motion and Vive cycle exclusion | Adopted in native fixtures. |
| Keep existing voice/profile and protected binding owners | Adopted; no capture/protocol rewrite. |
| Publisher request identification is sufficient | Adopted; no Cloudflare configuration changes. |

Final follow-up corrected the filter helper declaration order and null-frame guard.
ASan/UBSan default and input fixtures pass, including actual stick motion after
held-touch focus recovery and Vive custom-binding cycling. The fixed 16:1
anisotropy readback fixture requires 16x hardware; its 4x sampler-selection
check remains separate. This addresses Astra's final fixture capability finding.
