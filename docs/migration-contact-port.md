# Physical weapon contact migration decision

This is the implementation brief for physical melee and weapon/button contact on
the vkQuake-based `2.0` branch. Scope is all inherited behavior, not just stock
Quake. The solo-maintained branch should keep vkQuake's physics, QC VM, renderer,
and private-command queue as owners, using narrow adapters for donor behavior.
Physical headset testing is deferred; compile and local end-to-end fixtures still
belong in implementation.

## Verified facts and environment

| Claim | Evidence / status |
| --- | --- |
| The 2.0 contact payload already includes per-hand grip/base/tip, speed and weapon identity; mode and profile are separate offer constants. | **Verified by source:** `Quake/protocol.h` at the `vr_weapon_contact_t` declaration and nearby constants. |
| The client writer serializes populated contact fields unchanged; the producer must supply body-relative coordinates. | **Verified by source:** `Quake/cl_input.c` contact writer. |
| The server decoder bounds flags and rejects nonfinite components, then queues a receipt time. | **Verified by source:** `Quake/sv_user.c` contact decoder and `SV_QueuePrivateCommand`. |
| The OpenXR command producer currently fills aim/muzzle but not contact. | **Verified by source:** `Quake/vr_input.c:VR_InputPreparePrivatePose` and `VR_InputMove`; no contact producer found there. |
| The server only offers COLLISION/profile NONE and has no contact gameplay consumer. | **Verified by source:** `Quake/sv_main.c:SV_AppendWeaponContactProtocol`; no `SV_VRContactProcessCommand` in target `Quake/sv_phys.c`. |
| The donor producer derives rigid-point speed from tracked linear and angular velocity, with tracking/menu/weapon/discontinuity gates. | **Verified by source:** `../quakespasm-openvr/Quake/vr.c:VR_GetWeaponContactSample`. |
| The donor server validates command continuity and authoritative weapon/model identity, sweeps contact, and invokes a gated native QC button `touch`. | **Verified by source:** `../quakespasm-openvr/Quake/sv_phys.c:SV_VRContactProcessCommand` and `SV_VRContactButtonTouch`. |
| The donor also integrates model-exact melee profiles, native QC damage/cooldowns and parry. | **Verified by source:** donor `vr.c`, `sv_phys.c`, `vr_melee_*.h`, and `pr_exec.c`; individual profile equivalence on 2.0 is **unknown**. |
| OpenXR's tracked devices expose linear/angular velocity and validity flags. | **Verified by source:** target `Quake/vr_openxr.h:vrxr_device_t`. Physical provider accuracy is **unverified**. |

The reference is the pinned current product in `../quakespasm-openvr`, including
stock and mod-specific melee, akimbo, button contact, collision offer policy,
and native fallback. The target uses a private dialect and bounded command queue;
desktop and public-dialect play must remain on vkQuake's existing path. The
user's game data under `quakespasm_straight` is read-only for local fixtures.

## Adapter against transplant

| Design | Reuse / duplicated policy | Demonstrated incompatibility | Smallest vertical proof |
| --- | --- | --- | --- |
| Incremental contact adapter in command producer and server command consumer | Reuse target OpenXR sample, command codec/queue, existing traces and QC executor; port donor profile gates and strike state only where needed. No new protocol. | Donor OpenVR pose types, QuakeSpasm server frame/PMove and QC details differ from vkQuake. Exact command-owner insertion point still needs verification. | Stock axe tracked sweep reaches an authoritative stock target/button once, preserves native QC cooldown/damage, rejects stale/discontinuous samples, and leaves desktop/public peers unaffected. |
| Copy donor `vr.c`/`sv_phys.c`/`pr_exec.c` as whole modules | Duplicates pose, weapon, physics and QC owners. | No source evidence that wholesale replacement is needed. | Reject unless a bounded adapter proves impossible. |
| Client-only melee hit detection | Duplicates server authority and mod QC rules. | Trust boundary and multiplayer behavior disallow it. | Reject. |

## Decisions for senior review

1. **Contact insertion:** consume once in each command/frame owner, with one
   reset path for queue gaps, respawn, map changes and rejected identity. A
   generic hook on the latest command loses intermediate samples, while a hook
   in zero-time WALK maintenance replays them. **Review required:** exact frame
   drain and WALK-trial positions relative to native QC, retirement and time.
2. **First gameplay slice:** lean toward stock axe plus approved button `touch`,
   since both exercise server authority and native QC. An alternative is button
   only; it might be safer but does not prove melee. **Unknown:** whether stock
   axe model/animation evidence can be checked without importing a large pose
   subsystem first. This and insertion may be one coupled decision.
3. **Profile migration:** retain exact donor mod/QC/model pins and port by family
   after stock, never infer support from directory or weapon bit alone. Do not
   advertise MELEE until each enabled profile works. **Unknown:** which QC hook
   differences need a narrow shim, especially with vkQuake tasks and saves.
4. **Contact geometry:** use raw tracked grip and verified model edge, not the
   presentation-retracted muzzle, for physical speed/strike. Reuse calibration
   and collision resolver only where their coordinate spaces match. **Unknown:**
   whether the target retained enough model-vertex/animation state for exact
   donor edges.
5. **Scope guard:** stock-only is a vertical proof, not completion. Preserve
   akimbo and mod families in the feature map; keep collision opt-in until
   equivalent behavior and offer policy are restored.

Review depth: challenge the architecture and highest-risk command/QC ownership
issue, verify the load-bearing source claims, and seek deletion/simplification.
Do not re-review Vulkan, foveation, desktop rendering or historical feature-map
coverage. A runtime/headset certification is outside this design review.

## Astra senior-review disposition

Astra (`gpt-6-astra`, requested/reported `xhigh`) verified source and found a
critical ownership mistake in the draft: default physics executes a frame from
the latest command but the private queue can contain several contact samples.
The WALK trial also has zero-time maintenance that must not replay a strike.
The main integration pass spot-checked these two target paths, the missing
pending-record transfer and reset, the offer cache and the zero discontinuity
epoch in source. The review made no edits and ran no builds.

| Finding | Disposition |
| --- | --- |
| One hook at command acceptance/execution loses or repeats contacts. | **Adopt.** Use one shared consumer called by the default frame drain after `PlayerPostThink`/muzzle-pose restoration, through that frame's captured completed sequence; call it from the WALK trial only for a real run command after impacts/triggers and before its PostThink. Give contacts an independent last-visited sequence, including rejected samples. Do not add a queue or replay movement. |
| Stock axe damage can be replaced by an ordinary QC call. | **Reject that simplification.** Port a narrow donor native-QC outcome/readiness/trace adapter for the exact stock descriptor, including cooldown, scheduled callbacks, damage/sound and safe fallback. Do not import the whole QC VM. |
| Producing a sample is enough to reach the wire. | **Reject.** Extend `VR_InputClearPendingRecord` and `VR_InputApplyPending`; convert to body-relative once and define unsupported-weapon versus transient-tracking fallback. |
| Existing offer and teleport continuity are sufficient. | **Reject.** Cache the complete mode/profile offer, preserve the current single-player collision policy, and propagate a real relocation/discontinuity reset; the target's published epoch is currently zero. |
| Stock axe plus approved button `touch` is a useful first vertical. | **Adopt.** Button-only staging may be useful but cannot prove melee parity. Keep akimbo, parry and all verified mod profiles explicitly open afterward. |

The consumer uses command `msec` for physical effort, original receive time
for freshness, and QC time for cooldown. It must copy a sample before callbacks,
validate client/entity/weapon and relocation after them, and run outside the
temporary muzzle-origin scope. Stock geometry needs a verified model/animation
descriptor and the target trace-builtin seam checked before coding. The narrow
proof exercises damage, whiff, recovery, button guards, redundant/batched input,
tracking loss, relocation, left-handedness and movement-independent speed.
Physical headset equivalence and all mod profiles remain later gates.

## Model-data seam found during implementation

The donor's edge producer reads two vertices from the verified ready pose of
the original held model (`vr.c:VR_GetRawWeaponEdge`) and checks exact source
size/CRC and alias topology before doing so. The target still loads the original
MDL before optional MD3/MD5 replacements (`gl_model.c:Mod_LoadModel`), but its
retained `aliashdr_t` has no CPU `vertexes` stream or source-file identity
fields (`gl_model.h:aliashdr_t`); `Mod_CalcAliasBounds` consumes the classic
`poseverts` while loading. A pathname or current Vulkan vertex buffer is not
an equivalent identity/edge proof. The narrow adapter should validate the
original loaded bytes once at that loader boundary and retain only the verified
profile's two ready-pose edge coordinates and identity alongside the model's
lifetime. This avoids a second model loader or retaining whole pose streams.
Enhanced replacements need separate geometry/animation pinning before melee
admission; until then they retain native trigger behavior.

The edge cache alone is not a world-space contact pose. The target's held
viewmodel matrix is assembled in `Quake/r_alias.c:R_AliasModelMatrix` from the
loaded alias header, the calibrated held offset and scale, `vr_world_scale`,
`vr_gunmodelscale`, `vr_gunmodely`, entity scale and optional left-hand Y
reflection. `Quake/view.c:V_UpdateTrackedViewmodel` owns the tracked origin and
angles. The client producer must use that same transform (or a shared narrow
helper extracted from it) for both ready-pose edge points; applying only the
existing muzzle-offset transform would diverge from the visible axe. The
physics sample remains raw and unretracted, so presentation wall collision
must not shorten its physical stroke. This is a required parity check when
connecting the cached edge to the command producer.

The `2.0` model loader now caches the exact stock MDL ready-pose edge only
after matching source size/CRC, format, topology, connected vertex pair and
original scale. `Mod_GetStockAxeEdge` also rejects a selected MD3/MD5 skin
replacement. The record retains only the two model-local points and is cleared
on model reload/free. This does not yet send a melee sample or enable the
server's MELEE capability; the client transform and native-QC outcome adapter
are the next coupled implementation step.

## Collision-button implementation checkpoint

The first contact producer now sends a single anatomical hand's raw grip,
base and calibrated muzzle tip for stock ranged viewmodels when the private
server offers COLLISION and the local option is enabled. The existing private
codec transfers those body-relative fields. It uses a conservative physical
speed bound from OpenXR velocities; that bound is **not** an immersive-melee
strike speed. An explicit snap/180 turn or input reset sends an inactive
contact sample to break server history. No MELEE capability is advertised.

The server uses its accepted private-command queue, with one contact cursor
separate from movement retirement. Default frame physics drains through the
captured completed sequence after PostThink; the restricted WALK trial consumes
only a real command after impacts/triggers and before its PostThink. Two
consecutive valid samples are required. Bounded point traces cover the weapon
shaft, and both eye-to-grip and grip-to-button visibility are checked. Only a
`func_button` whose original zero-parameter `button_touch` is installed can
receive the native QC callback, once per entry. The callback runs outside the
borrowed muzzle-origin scope and restores QC `self`, `other` and `time`.

An Astra xhigh read-only code review found four defects in the first patch;
the integration pass checked its source claims and applied these dispositions:

| Finding | Disposition |
| --- | --- |
| A teleport callback cleared history but allowed already-accepted later contacts to rearm. | **Adopt.** Invalidate contact through the accepted-command cursor, retaining the movement queue; hook player `setorigin` and direct host placement. A callback that moves/kills the player stops further contact while the ordinary death observer can still run. Direct QC writes to player origin outside these hooks remain a qualification gap. |
| Snap turning can create an artificial physical sweep. | **Adopt.** Suppress one outgoing contact after explicit snap/180 turns and input resets; smooth turning remains continuous. |
| Endpoint traces miss a small button crossed by the shaft. | **Adopt.** Use bounded 3-unit-spaced point sweeps plus a current-shaft trace, sharing the donor's point-hull policy. |
| A two-unit reach tolerance could accept a wall instead of the target button. | **Adopt.** Require any reach blocker to be the same button; do not accept a different intervening entity. |
| Rechecking button function identity immediately after `ED_Retain` adds no protection. | **Adopt.** Remove that duplicate check. |

The complete Linux SDL3 Makefile link passes with `-Werror` after these fixes;
`git diff --check` is clean. This is source/build evidence, not a verified
in-game button activation or melee parity result. Real target/button behavior,
body relocation paths beyond the explicit hooks, haptics, all other supported
weapon models, model-exact stock axe, mod QC families, akimbo and parry remain
open. Keep the current local opt-in and server policy until those gates close.
