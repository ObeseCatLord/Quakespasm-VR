# Stock predictive movement when joining in water

Status: planned before implementation. This completes a startup restriction
left over from the original dry WALK trial; it does not change the movement
solver, command scheduler, wire format or cooperative QC replay permissions.

## Source evidence and reuse

- `Quake/sv_main.c:SV_PrivateWalkTrialAdmissionFailure` rejects a stock player
  with nonzero `waterlevel` before selecting the session. Selection occurs only
  at `Host_Begin_f`; the player cannot regain it simply by leaving the water.
- `Quake/sv_phys.c:SV_PrivateWalkTrialClassifyOwner` already classifies stock
  WALK with the ordinary hull as WALK regardless of water depth. The existing
  owner validator checks finite water levels in the supported 0..3 range before
  initial selection. It also rejects invalid ground references and customphysics.
- The stock liquid implementation and `sv_main.c:SVFTE_WriteEntitiesToClient`
  already allow ordinary stock liquid replay. Actual per-command validation,
  PMove, water/ledge timers and publication remain their existing owners.
- QSS-M's actual `Quake/sv_user.c` independent input dispatch and the primary
  VR `Quake/sv_phys.c` standard-player PMove path have no dry-session admission
  condition. Their solvers support water; copying another dispatcher would
  duplicate the migrated command owner.
- `Quake/host_cmd.c:Host_Begin_f` uses the same selection entry for new and
  restored players. The existing local/load plan already establishes that
  entry's profile, identity and command-reset ownership.

## Implementation and acceptance

Remove only the stock initial `waterlevel != 0` refusal and its dry diagnostic.
Retain WALK/SOLID_SLIDEBOX eligibility, observational begin validation, the
pinned profile, robust elevator requirement, custom-stat disjointness, lifetime,
ground and input checks. Other supported native states continue using their
existing authority; no broader replay permission is inferred from selection.

### Saved ledge-jump boundary (source follow-up before implementation)

`SV_PrivateWalkTrialSelectAtBegin` resets the private waterjump timer. A saved
stock `FL_WATERJUMP` and `teleport_time` cannot simply become a private timer:
the selected solver seeds from `private_pmove_waterjump_secs`, and its existing
policy explicitly distinguishes a witnessed solver-owned jump from QC state.
Dropping the dry restriction alone would let the solver clear a saved jump.

At the existing `SV_Physics_Client` frame dispatcher, select its native frame
when the pinned stock player has `FL_WATERJUMP` but no private waterjump timer.
`SV_Physics_ClientSelectedNativeFrame` already stages native input, consumes
roomscale once, updates the native jump through `SV_ClientThink`/`SV_WaterJump`,
and publishes native authority. After QC clears the flag, the ordinary WALK
dispatcher resumes on the next frame. Keep an existing positive private timer
on its current selected path. Observe only before callbacks: a provisional
waterjump created by that command's PreThink must still reach the existing
stock reconciliation/solver path.

This uses existing authority and native time owners. Reject seeding a new
private timer from an arbitrary QC flag/deadline or adding a separate recovery
state. Production ownership extends narrowly to `Quake/sv_phys.c`'s initial
client dispatcher. Source review must check this ownership boundary before
commit. Runtime acceptance additionally includes a restored active native
ledge jump, its expiry and subsequent selected movement.

Keep the user's modified `docs/migration-2.0.md` untouched. No renderer changes
are needed.

At the end of full implementation, qualify actual new-session and restored
stock players at water levels 1, 2 and 3 through begin, receipt, physics,
snapshot and client replay, including leaving water, ordinary jump/ledge jump,
desktop/VR and a public peer. Malformed water values, wrong hull/customphysics,
stale ground, terminal spawn and disabled/private/public profiles must retain
their existing refusal or native path. Reuse existing native fixtures and
collision assets; do not create a parallel harness or copy licensed assets.
Source review is permitted now; builds and tests remain deferred.
