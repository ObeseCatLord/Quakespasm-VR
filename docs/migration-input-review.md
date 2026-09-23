# Controller input increment

This increment connects completed OpenXR action samples to vkQuake's existing
key bindings and command queue. It does not port analog locomotion, turning,
tracked hand/muzzle commands, roomscale, the weapon wheel, menu pointers or
haptics. It therefore does not close VR-006, VR-008, VR-009 or XR-003.

## Behavioral references and owners

The inherited input policy is `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`,
`Quake/vr.c`: logical key ownership at 863, handedness at 10801, strongest-axis
selection at 10887, deadzone/exponent at 10939, trigger hysteresis at 11036,
menu axes at 11190 and bindings at 11210. The existing OpenXR adaptation is
`3080841333fa94000df7e1fb9e549c7158685dd6`, `VR_ReadControllerState` at 10094.
Its mapping must be composed with the inherited policy, not inferred from
legacy names such as `SteamVR_Touchpad`.

| OpenXR action | Logical offhand | Logical weapon hand |
| --- | --- | --- |
| Trigger | `LTRIGGER` | Gameplay: `RTRIGGER`; menu: Enter; binding capture: `RTRIGGER`; native modal grab: `ABUTTON` |
| Grip | `LSHOULDER` | Index: `RSHOULDER`; other profiles: `VR_ALTFIRE` |
| Vive pad click; other profiles' stick click | `LTHUMB` | Index: `VR_ALTFIRE`; other profiles: `RTHUMB` |
| Index pad press | `YBUTTON` | `YBUTTON` |
| Primary | `ABUTTON` | `XBUTTON` |
| Secondary or menu | Escape | `BBUTTON` |

Both hands may contribute to `YBUTTON`; releasing one must not release the
other's contribution. The backend currently supplies no extra-button bindings, and vkQuake has no
`JOY1`–`JOY4` key names; those unused source extension slots remain unported.
Handedness swaps logical roles only; runtime physical hand/device identity stays
unchanged. This source mapping is not hardware qualification of any profile.

OpenXR owns action synchronization and physical inputs. The host consumes the
completed sample before `Cbuf_Execute`, retaining input-before-render cadence.
`Key_Event` retains binding/menu/alternate-binding ownership; the adapter keeps
only edge, role and neutral-rearm state. `Key_ClearStates` releases native keys
before forgetting adapter ownership. A failed XR begin must not expose a
partially populated input sample. Eye tracking and foveation are independent.

## Architecture comparison

A full `vr.c` transplant would introduce OpenGL/OpenVR dependencies and adjacent
weapon, menu, tracking and haptic systems before their vkQuake integration points
are ready. The demonstrated incompatibility here is only the runtime-input type
boundary. A narrow adapter composes the existing mappings directly without an
intermediate OpenVR controller struct or another action/frame state machine.
Native keys, bindings, command construction, transport and server gameplay remain
reusable. The end-to-end check injects at the action boundary and observes actual
movement and firing in a stock map; it must not write kbutton, usercmd or packet
fields as a substitute for that path.

## Review and verification

The local design review used explicit Astra/Max, with effective model and effort
verified by the orchestrator. Its source claims were independently spot-checked.

| Recommendation | Disposition |
| --- | --- |
| Use a narrow adapter and existing frame/key/command owners | Adopted; no runtime shim, second sampler, or copied command pipeline. |
| Compose existing OpenXR mappings rather than infer physical controls from legacy names | Adopted; the initial Index-pad alternate-fire guess was rejected against source. |
| Keep trigger-based menu activation and native binding capture distinct | Adopted; Enter selects an item, while capture receives `RTRIGGER`. Pointer support remains pending. |
| Refresh input inside blocking confirmation dialogs | Adopted through serial `SCR_UpdateScreen(false)` and the same adapter. Modal right trigger supplies the existing confirmation key; Escape/B retain cancellation. |
| Stop a batch when callbacks change destination, capture or grab state | Adopted, including neutral rearm and input snapshots taken before callbacks. |
| Invalidate input on failed or skipped sampling | Adopted at failed begin, resource-creation failure and loading suppression; valid focused nonrendering samples still work. |
| Release an old held binding before replacing it | Adopted in native `Key_SetBinding`; the adapter does not own binding strings. Identical rebinding remains a no-op. |
| Rewrite native input for independent SDL/XR producer identity | Deferred. The donor has one state per key, so simultaneous SDL/XR holds of the same key are not independently arbitrated. |

After the coherent menu follow-up, the Linux build and production-adapter
ASan/UBSan checks pass. The native key-name fixture passes. An initialized stock
map driven through injected action samples moved the authoritative player and
consumed shells (25 to 22); focus loss released movement and attack, held input
remained suppressed on refocus, and a fresh press fired again (20 shells).

The isolated simulated-Monado dialog check passed confirmation, cancellation and
timeout. It observed 6, 4 and 9 XR frame begins respectively without a host-frame
advance. The held-entry confirmation case required release/repress. Native grab
state was released afterward. The actual loading-screen early return cleared
focused input while retaining pose metadata. These are software integration
checks with action-boundary injection, not physical-controller or HMD proof.
Reproduction and precise fixture boundaries are in [tests/README.md](../tests/README.md#openxr-controller-button-input).

The final local Astra/Max review found three additional modal/menu combinations.
The main agent verified each against the native owners before implementation:

| Final review finding | Disposition |
| --- | --- |
| Destination alone misses menu-to-menu transitions | Adopted: include the existing public `m_state` in the input context, with no new menu state machine. |
| Unrelated modal buttons can overwrite a recognized decision | Adopted: emit only supported modal decision keys; cancellation takes precedence over simultaneous confirmation. |
| An active ALT modifier can transform modal confirmation into an unrecognized key | Adopted in native new-press routing only while the modal input grab is active. Existing held-key release association and ordinary menu binding capture remain intact. |

After these fixes, the initialized native lifecycle probe passed all 55
checkpoints: alternate-key release, shared Y ownership, role/focus/inactive-hand
transitions, held-trigger rebinding, modal-grab neutral rearm, native key clears,
real `M_Keys_Key` binding capture and single-sample submenu boundaries. The test
records the original trigger binding and verifies it stays unchanged until a
fresh capture press; selecting another action does not clear unrelated bindings.

The expanded isolated-Monado check passed all seven modal cases: yes/no/timeout,
confirm/cancel with simultaneous unrelated controls, and confirm/cancel with an
actual native ALT modifier still active during the blocking dialog. The native
modifier release command executes afterward. Cancellation wins if confirm and
cancel are sampled together. The loading invalidation check also passes.
The final Linux build passes, and no temporary service or game process is left
running. The compatible release build, device testing, native Windows and ARM64
remain deferred. No performance improvement is claimed for this increment.

The follow-up local Astra/Max review accepted this bounded increment after
verifying the actual native and simulated-Monado result files. All three P2
findings above were closed with no remaining blocking findings in that fix
scope. This acceptance does not expand the device, platform, performance or
full-migration claims.

## Local-player sound haptic checkpoint

The inherited local-player interaction-sound filter now drives the existing
OpenXR haptic output from a fully decoded sound packet. It keeps footsteps and
player damage sounds out, excludes remote/world entities, and maps the logical
weapon hand through the existing left-handed setting. The archived `vr_haptic`
setting defaults on and suppresses this path when off; desktop and unattached
OpenXR sessions remain inert. The pulse is the inherited five milliseconds.
Paired akimbo off-hand pulses, weapon-wheel/menu feedback and a VR-menu toggle
remain with their respective presentation ports. The strict Linux Meson and
Makefile `vkquake` builds pass after adding the already-used weapon schema and
calibration objects to the Makefile. No device haptic behavior is claimed yet.

## Menu haptic checkpoint

The OpenXR key bridge now emits the inherited short menu pulse on rising
navigation/select/back input, including Escape opening the menu from gameplay.
It uses the contributing hand's logical role through the existing handedness
adapter and `vr_haptic` master toggle. Repeated held keys, releases, shared-key
contributions from a second hand, binding capture and native modal grabs do not
request a pulse. The haptic request precedes the corresponding native
`Key_Event`, preserving the donor's event order and the bridge's context-change
invalidation. This is controller-feedback parity only; it does not add a VR
menu option for the toggle or establish device output on Monado or other runtimes.
