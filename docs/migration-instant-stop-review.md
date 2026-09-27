# Optional VR instant-stop movement: senior review

This is a migration design for the source `vr_movement_instant_stop` setting.
The classic and selected private movement paths are implemented on `2.0`.
Live client/server and headset qualification remain pending. The setting
defaults off in the source and in `2.0`. Desktop movement retains vkQuake
behavior.

## Verified movement owners

- The product source registers `vr_movement_instant_stop` in `SV_Init`. Its
  classic `SV_AirMove` zeroes horizontal velocity for a grounded VR player when
  effective wishspeed is zero and Gorilla movement is ineligible. The source
  condition is at `master:Quake/sv_user.c` near `SV_AirMove`.
- In `2.0`, conventional clients still use `Quake/sv_user.c:SV_AirMove`.
  Selected private clients use `Quake/sv_phys.c:SV_PrivateWalkTrial` and the
  shared `Quake/pmove.c:PM_PlayerMove`; the latter is also used by client
  replay in `Quake/cl_main.c`.
- `Quake/sv_user.c:SV_AirMove` now applies the default-off setting only to
  active VR commands on the classic path. It retains shallow-water stopping
  and excludes native ladder contact, which this fork can route to `SV_AirMove`.
- The private server already exports movement flags through `STAT_MOVEFLAGS`
  (`PMSV_ExportMoveStats` and `SVFTE_WritePrivateMoveStats`).
- Classic source movement is processed before `PlayerPreThink`. The selected
  private path runs `PlayerPreThink` and weapon Think before copying callback
  velocity into PMove. A stop inserted after those callbacks could erase a
  newly authored QuakeC impulse. This order was independently checked against
  `master:Quake/host.c`, `master:Quake/sv_phys.c`, and current
  `Quake/sv_phys.c:SV_PrivateWalkTrial`.
- PMove prepares Gorilla state before friction and may subdivide a command;
  later substeps reuse the prepared state. Checking only fresh hand action or
  raw zero stick input can erase Gorilla momentum.

## Design disposition

| Astra recommendation | Disposition | Reason |
| --- | --- | --- |
| Reuse classic movement and shared PMove with a narrow adapter | Adopt | These are the existing authoritative and predictive owners. |
| Keep the default-off switch server-authoritative | Adopt | This matches the source's server-local control; independent remote preference would require a separate negotiated feature. |
| Put instant stop unconditionally in PMove friction | Reject | It can erase a QuakeC impulse that survives the source's earlier stop point. |
| Exclude only current Gorilla hand action | Reject | Prepared Gorilla state persists across subdivided command steps. |
| Allocate an unverified movement-flag bit immediately | Adapt | The implementation uses an unused private high bit and requires a reliable feature offer/capability reply before the server transmits it. The client masks that bit without the offer/reply. Older private peers retain default friction. |
| Copy only the classic server branch or suppress velocity only on the client | Reject | Both omit a movement owner and cause authority/prediction mismatch. |

The minimal implementation should keep one stop predicate based on **effective
wishspeed**, after backward-teleport suppression and WALK vertical-input
removal. Grounded shallow water still qualifies; swimming, ladders, airborne
movement, and eligible Gorilla movement do not. Preserve jump ordering and
the separate room-scale sweep. Apply the classic behavior in its existing
server owner. For selected private movement, place the authoritative stop
before QuakeC callbacks, then let subsequent PMove preserve callback-authored
velocity; the predicting client needs the corresponding stop from a negotiated
or otherwise proven movement setting. Do not create a parallel movement owner.

The implementation puts the classic stop in `SV_AirMove` and the selected
server stop before `PlayerPreThink` in `SV_PrivateWalkTrial`. The later server
PMove pass skips a second stop so callback-authored velocity survives. Client
replay applies the stop on its first command substep, before Gorilla and
friction. A `vr_instant_stop_protocol 1` offer and `vr_instant_stop_cap 1`
reply gate the private movevars flag; `PMCL_SetMoveVars` ignores it for public
and older private peers. On a received rule change, prediction waits until
commands sent under the previous rule have been acknowledged. The existing
`STAT_MOVEFLAGS` transport carries the rule without a new movement protocol.
The server checks one-unit ground support after the room-scale sweep before
stopping: that sweep intentionally restores the previous ground flag, whereas
client replay categorizes ground at the new position. The check follows the
client's slope retry, upward-velocity exclusion, and optional `pm_pground`
rule. It avoids stopping momentum when physical motion carries the player off
a ledge or omitting a stop when the sweep reaches support.

The remaining acceptance cases are grounded idle/release, release plus jump, shallow
water, ignored WALK upmove, teleport-suppressed backmove, room-scale
displacement, Gorilla coasting across substeps, callback-written impulse,
desktop movement, and default-off behavior. Compare server and client replay
for the selected path. Physical headset checks and performance measurement
remain the user's later gates.
