# Inherited aim integration review

This P1 increment ports head/mouse aim through existing vkQuake owners. It does
not complete tracked controller weapons, roomscale authority, full scripted
camera/demo behavior, or the QSS-M command/prediction migration.

## Behavioral reference and architecture decision

The product reference is `1327f795cc2e3a8e4f7c9d68e31d64383930cc00`.
Its `Quake/vr.c:2877` pose conversion, `:10197` aim switch and `:10290`
history update supply the arithmetic now in `Quake/vr_aim.h`. The original
GPL notices and matrix-helper attribution remain. Defaults are inherited:
`vr_aimmode=7`, `vr_deadzone=30`, both archived. Modes 1–6 implement head,
mouse and blended aim; mode 7 currently resolves head view with no controller
substitute. Its hand-derived command aim requires the subsequent coupled port.

Source `vr.c:9199` separates setview readback from the controller yaw target;
`:9213` classifies server yaw only after the complete message. `:10771`
synchronizes authoritative aim/view/history, while `:10778` centerview copies
visible pitch/yaw into aim without zeroing physical tracking. The aim-mode
callback at `:2994` clears mode-specific yaw requests rather than pose history.
The source's noncontroller readback computes this frame's orientation before
changing its alignment; that ordering is retained.

A full transplant of `VR_UpdateScreenContent`, input writers and the product's
separate command-angle storage would duplicate donor frame/input ownership and
pull in unported hand, weapon and movement policy. The minimal adapter instead
keeps donor `cl.viewangles` as command/input aim and reuses its keyboard, mouse,
joystick, command construction and network path. The existing view owner stores
the visual angles, previous aim/head samples and pending alignment requests.
It publishes once on the main thread after successful frame begin, before view
and draw tasks. No new renderer, command clock, wire format or camera state
machine is added. Commands retain the existing input-before-render cadence.

The renderer maps raw tracking through the resolved view basis times inverse
raw head orientation. This lets runtime eye transforms enter the desired view
exactly once. The existing chase calculation receives resolved visual angles;
its collision position and look-at result survive. The saved base records which
angle contribution was used, allowing fresh head rotation during pause.

Donor `svcfte_setangledelta` rotates existing aim/view histories at the parser
boundary. Donor angle locks remain authoritative for outgoing commands. A small
withheld-aim correction retains physical movement while locked; unlock publishes
it once. Absolute angles and centerview replace aim and clear that correction.
This is an adaptation to donor locks, not a claim that the source has identical
lock timing or complete scripted-camera semantics.

## Astra disposition

Astra (`gpt-6-astra`, explicitly `xhigh`, effective settings verified) reviewed
the source and actual diff read-only. Terra implemented the pure resolver and
its fixture in an exclusive write scope; main reviewed that output, implemented
the integration and ran checks. Astra confirmed the existing-owner architecture
remains appropriate and reviewed the follow-up fixes.

| Finding | Disposition |
|---|---|
| Tracking-origin replacement must not reset a game or erase visual/aim separation. | **Adopted.** Preserve histories, rebase the new raw yaw onto the preceding mapped head yaw, and retain input received during tracking loss. Same-world runtime retirement requests rebasing; client clear/disconnect resets state. |
| Mode changes and centerview have different source semantics from recenter/reset. | **Adopted.** Mode callbacks retain pose history and pending reference rebases. Centerview copies visual aim/history, clearing any already-incorporated withheld aim. Source mode changes can intentionally change the aim/view relationship; universal visual continuity is not promised. |
| Accepted server yaw must supersede a simultaneous reference rebase, and invalid poses must not consume requests. | **Adopted.** Reference alignment is resolved first, accepted authoritative yaw second; requests wait for valid tracking. Absolute and relative messages keep separate meanings. |
| An accepted gameplay target can become stale while frames are skipped or angle locks expire. | **Adopted correction to source intent.** Validate at every completed message and before application. One provenance bit distinguishes accepted server targets from standalone setview. Hidden weapon cancels the former even after the lock expires; intermission cancels either. This is stronger than the source's transient `fixangle` predicate. |
| Frozen commands must not discard accumulating physical head rotation. | **Adopted.** Withheld resolved aim accumulates while the donor lock freezes command angles, then transfers exactly once on unlock. Pure source arithmetic is unchanged. |
| Chase must consume visual head orientation without applying it twice or losing its collision result. | **Adopted.** Feed visual aim into the real donor chase calculation and subtract only the saved contribution during subsequent stereo preparation. Paused frames receive the latest orientation delta. |
| Prepared-camera validity cannot share the player-floor classification flag. | **Adopted.** A separate validity bit is cleared for client reset, forced console and intermission. A tracked paused client with no valid base prepares one before replacement; later paused frames retain it. |

Astra's final focused inspection reported no remaining actionable findings in
the last centerview and prepared-base fixes. Review is not independent runtime
execution; the integration evidence below was collected by main.

## Local verification

- Linux SDL3 build passed with no compiler warnings reported.
- Pure resolver fixture passed pose signs, all seven mode branches, blended
  deadzone, absent controller aim and nonfinite pose rejection.
- Production camera fixture links the real `CL_BaseMove` and chase calculation.
  It checks single head contribution, command aim, reference loss/recovery,
  mode changes, absolute/relative angles, server lock accumulation/unlock,
  centerview during lock expiry, request cancellation/precedence, client reset,
  and first-frame-paused preparation. The chase hull trace supplies known
  collision points; it does not model real map physics. Existing floor, scale,
  comfort, paused restoration and abort boundary checks also pass.
- Isolated simulated Monado/RTX 4090 run passed 24 probes over 2,111 scene frames,
  exiting normally with zero Vulkan validation errors or synchronization hazards.
  The matrix covers effective OIT/MSAA/indirect modes, task rendering, resize,
  worldscale/floor changes, head/mouse aim, local-server firing and real chase
  with running and paused simulation. Head aim reached server angles and firing
  consumed shells. Rendered chase yaw matched the prepared collision-camera
  contribution plus current head motion.
- Both-eye head-aim and paused-chase captures were inspected. The renderer fills
  both eyes, retains stereo differences and shows the expected world/player.
  Existing donor HUD/weapon presentation is still visible; this is not approval
  of final VR placement or UI behavior.

The 24-probe GPU run preceded the final centerview and invalid-paused-base fixes;
those two fixes were verified by the expanded production fixture and final Linux
build. The private test service was stopped. Runtime configuration, user game
configuration and product `master` were not modified. Windows and ARM remain
explicitly deferred; headset/eye-tracking tests remain later checkpoints.

## Remaining acceptance

This is partial VR-005/VR-014 progress, not P1 completion or a performance result.
Next integrate the existing hand-pose, movement/roomscale, weapon and command
semantics together, retaining a single authoritative command path. Default mode
7 must gain real tracked hand aim before gameplay acceptance. Full scripted
camera/demo transitions, menu/HUD, reference/runtime recovery, independent avatar
and shadow poses, moving brushes and eye-only visibility remain on the feature
map. Eye tracking stays optional; fixed foveation remains explicit opt-in only
and never a fallback.
