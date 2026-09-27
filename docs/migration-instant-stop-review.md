# Optional VR instant-stop movement: senior review

This is a migration design for the source `vr_movement_instant_stop` setting.
The classic server path is implemented on `2.0`; selected private predictive
movement remains pending, so the feature is not yet fully migrated. The
setting defaults off in the source and in `2.0`. Desktop movement must retain
vkQuake behavior.

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
| Allocate an unverified movement-flag bit immediately | Defer | The candidate low bits have historical meanings and an older admitted private client could predict the wrong stop rule. |
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

Before implementation, resolve the exact shared predicate placement and
mixed-build contract. In particular, prove that a newly accepted server
setting is not applied to an outstanding replay command with a different
movement rule. Use the existing movement-stat transport if its flag namespace
and admission behavior are verified; otherwise use the narrowest compatible
revision. This is a compatibility proof, not a request for a new general
protocol layer.

The acceptance cases are grounded idle/release, release plus jump, shallow
water, ignored WALK upmove, teleport-suppressed backmove, room-scale
displacement, Gorilla coasting across substeps, callback-written impulse,
desktop movement, and default-off behavior. Compare server and client replay
for the selected path. Physical headset checks and performance measurement
remain the user's later gates.
