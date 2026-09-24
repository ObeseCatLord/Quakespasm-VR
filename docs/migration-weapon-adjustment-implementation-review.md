# Astra review brief: controller weapon calibration on vkQuake 2.0

## Goal and scale

Review the implemented OpenXR controller grip/muzzle adjustment transaction for
behavioral and architecture mistakes before calling this slice done. This is a
solo game-engine migration, not a new calibration subsystem. Device behavior
checks belong to the user later; Windows builds are deferred by request.

## Environment facts to verify first

| Claim | Evidence |
| --- | --- |
| The inherited command flow freezes a weapon, uses the *frozen* rotation for grip and muzzle inverse recentering, consumes the trigger, and holds the post-muzzle pose until the hand returns within eight Quake units. | [verified: `../quakespasm-openvr/Quake/vr.c:7599-7960`] |
| One target calibration owner holds classic/enhanced/MP offsets and now owns both adjustment commands and the format-selected schema rewrite. | [verified: `Quake/vr_weapon_calibration.c:60-95,625-780,880-1055,1170-1690`] |
| The Vulkan alias renderer obtains the effective held offset from that owner and applies `c=(vr_world_scale/.75)*vr_gunmodelscale`, plus header origin, `vr_gunmodely`, left-hand reflection and alias/entity scale. | [verified: `Quake/r_alias.c:661-724`] |
| The input owner suppresses only the dominant trigger during adjustment, retaining suppression through a physical release; its full-hand neutral gate remains for context/focus loss. | [verified: `Quake/vr_input.c:3120-3175,3279-3425`] |
| The view owner freezes only the presentation viewmodel pose and skips presentation collision; command/contact poses remain live. The cue uses the existing stereo crosshair snapshot. | [verified: `Quake/view.c:459-544`; `Quake/gl_rmain.c:1128-1165`] |
| The active schema parser supports classic and enhanced fields. The initial line-based writer could lose the other family's tokens in compact layouts; token-span rewriting and a parsed-value check now correct that. | [verified: `Quake/vr_weapon_schema.c:299-370`; `Quake/vr_weapon_calibration.c`, `VR_CalibrationWriteUpdatedBlock` and `VR_CalibrationSavedValuesMatch`; focused save fixture] |
| Linux vkquake compiles and links. Schema parser and calibration reload fixtures pass; the earlier reload linker failure was resolved by separating command registration from slot initialization. | [verified: local `ninja -C build -j4 vkquake`, fixture commands in `tests/README.md`; no runtime headset claim] |

## Decisions for review

1. **Keep one calibration owner/session, with view/input hooks at their existing owners.** Lean: retain. Alternative: separate UI manager rejected because it duplicates slot/model policy. Audit whether any new state or validation should instead be deleted or narrowed.
2. **Commit offsets from frozen orientation, with effective MP overlay subtracted from the edited base.** Lean: retain. Alternative: live orientation rejected because wrist movement changes the saved target. Audit renderer/aim transform consistency, including left-handed, MD5/MD5_8, scale and missing enhanced muzzle base.
3. **Keep trigger suppression through the muzzle return-to-grip hold.** Lean: retain as a safety improvement while a gun is visually frozen. Donor holds the pose but its trigger consumption may stop after commit. Audit whether this introduces input leakage or a severe behavior mismatch.
4. **Keep format-selected schema rewrite in the existing owner.** Lean: retain. Alternative: a second schema file/registry rejected. Audit preservation of comments, global and other-format fields, duplicate model blocks, and rollback on uncertain write status.
5. **Repair the obsolete standalone fixture later at a narrow boundary.** Lean: fix test harness/registration seam without moving production state. Alternative: broad module split solely for test linking rejected. Audit whether this is masking a meaningful production coupling problem.

The trigger and return-hold decisions overlap: if the frozen pose should not
accept attack, they may be one rule. Merge them if that simplifies the policy.

## Review scope and output

Verify these claims against the code, then rank only the high-leverage findings.
Depth budget: the calibration transaction, its view/input/cue hooks, and schema
rewrite. Do not re-review OpenXR lifetime, foveation, server netcode, all weapon
profiles, or the whole vkQuake migration. Return concrete file/line evidence,
severity, the smallest correction or deletion, and any actual human decision.
Do not edit files or perform hardware/Windows testing.

## Main disposition

| Astra recommendation | Decision and reason |
| --- | --- |
| P1: line-based schema rewrite can delete other-format tokens or insert after a compact closing brace, with parse-only validation claiming success. | **Adopted.** The rewrite now uses token spans from `COM_ParseExBufferSpan`, preserves unedited byte ranges, inserts before the closing-brace token, and verifies parsed requested values before writing. The focused fixture exercises compact, mixed-format and comment-adjacent layouts. |
| P2: saved classic grip can change muzzle availability on reload; new blocks after global directives inherit offsets absent from the live slot. | **Adopted.** Classic grip seeds a missing muzzle before saving, and the shared save path publishes classic muzzle presence with rollback on failure, including manual saves and muzzle recentering. New weapon blocks are prepended before global directives. The fixture checks absent-muzzle serialization and new-block global isolation. |
| P2: frozen presentation still allows live melee/collision contact publication. | **Adopted.** The input owner suppresses contact preparation and pending acceptance during all adjustment phases, clears pending contact, and restarts sweep continuity at entry/exit while keeping tracked movement live. |
| Reload fixture linkage suggests a registration boundary problem, not a second calibration subsystem. | **Adopted.** Command registration is called by `SV_Init` after slot initialization; the calibration reload fixture now links and passes without a new owner. |

No human decision was needed. Astra ran locally as `gpt-6-astra` at `xhigh`
and reported verifying those settings from session metadata. The remaining
behavioral gate is live headset use, which the user has reserved for later.
