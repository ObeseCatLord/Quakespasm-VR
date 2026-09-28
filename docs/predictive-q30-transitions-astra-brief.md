# q30 native transition design brief

Working brief for the remaining native-state stage of
[ordinary q30 replay](predictive-q30-replay-2.0-plan.md). This is a solo-maintained
engine; keep one movement owner, queue, completion cursor and QC lifetime.
Admission remains closed until the complete normal-session contract works.

| Environment fact | Evidence / boundary |
| --- | --- |
| Authoritative workspace | `quakespasm-2.0`, branch `2.0`; main is outside the write scope. |
| Current checkpoint | `8a0871c1`: negotiated ordinary policy, complete inputs and actual replay consumer. |
| Exact program | Installed q30a1024, 2,347,206 bytes; SHA256 `5e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340`. |
| References | Actual bytecode is authoritative. `/tmp/q30-source-check/*.qc` is source-like explanatory evidence, not a matching source certification. |
| Local verification | Existing actual-QC/native and mixed transport/parser/replay fixtures; no headset, Windows/ARM or performance measurement in this pass. |
| Protected user edit | `docs/migration-2.0.md` must not be edited or staged. |

## Verified facts

- [verified: source] `SV_PrivateWalkTrialClassifyState`, receipt's
  `FrameStateError`, dispatch and snapshot permission currently understand stock
  terminal/frozen, ordinary WALK, FLY and NOCLIP. q30 admission/permission are
  explicitly closed. `Host_Begin_f` selects before `client->spawned` becomes
  true; initial q30 startup needs the same body predicate without inventing
  another selection lifetime or temporarily changing `spawned`.
- [verified: source] A selected ordinary command updates angles, performs native
  auxiliary roomscale, categorizes water, applies grounded stop, then actual
  PreThink, scheduled Think, shared PMove, impacts/triggers/physical contacts and
  PostThink. Native selected frames call `SV_ClientThink` before the fresh native
  dispatcher: native acceleration therefore precedes roomscale and PreThink.
  Calling ClientThink after a QC zero/force is behaviorally different.
- [verified: source] Existing phase continuation only handles terminal/stock
  freeze. It restores the whole world duration, skips completed callbacks and
  commits the selected head once. Living arbitrary q30 WALK cannot simply use
  that continuation: ordinary selected PreThink had no native input acceleration.
  After movement, do not run another native interval; finish PostThink once,
  mark native, clear credit and stop batching, retaining later heads.
- [verified: bytecode definitions and instructions] q30 `chaoscount` starts 0,
  startup executes while <=2; `prethink`/`postthink` start 0. Camera globals are
  `intermission_running`, `secloc_running`, `cinematic_running`. `skill`/`oskill`
  have transition side effects. `map_jumpheight` remains the ordinary live input.
- [verified: bytecode] Jump boots eligibility is `moditems & 1048576`; grapple
  eligibility is `moditems & 128` before inspecting `hookent` and CT_GHOOKPROJ
  190. Actual super-shotgun weapon value 2 can subtract `v_forward * 50` from
  airborne velocity. Prefer native eligibility before activation, rather than
  trying to subtract authored forces or detect only already-active hooks.
- [verified: bytecode / source-like cross-check] ladder PreThink clears the
  owned `onladder` latch before damping/jump; actual triggers rearm it. Hold
  `pausetime` zeros velocity after jump. PostThink's nonempty `target2` invokes
  arbitrary targets, and camera PostThink can relocate/fix angles.
- [verified: source-like branch and bytecode inflictor comparison] burning and
  poison invoke T_Damage with world as inflictor; its knockback branch requires
  a non-world inflictor. Do not blanket-classify all debuffs as movement forces
  from the damage function name. A complete branch verification is still needed
  before asserting their ordinary cadence compatibility.
- [verified: source] Typed VM-owned hash-map lookup already exists. No field cache
  is needed. `GetEdictFieldValue` does not upper-bound a supplied offset: typed
  helpers must check the current program's field/global bounds first. Entity
  values are integer byte offsets; validate structurally before dereferencing.
- [verified: source] Existing raw Gorilla eligibility can defer acceleration
  until after QC, so hold/camera qualification must also fence hand movement.
  `SV_PrivateWalkTrialWaterjumpCallbacks` currently clears flag/deadline edits;
  that stock adapter must not erase q30's native waterjump ownership.

## Mostly worked design and unresolved phase decision

One pure current-q30-state predicate in `sv_phys.c` extends the existing enum.
Valid native ability/startup/hold/camera/hull states remain selected but execute
the existing native frame; malformed finite/type/reference inputs remain
rejected. Receipt and snapshots remain observational. At the physics turn,
fresh water categorization precedes dispatch. Initial admission uses the same
internal body predicate with the existing known-to-QC pre-begin lifecycle.

Native-before-QC and native-after-movement are clear. The open fork is a dry
ordinary command whose horizontal roomscale enters water before QC, or a
callback that newly selects a living ability before movement. Native input must
retain its original order; rerunning QC, adding a world interval after a command
interval, or accelerating after authored velocity edits are not acceptable.

1. **Minimal existing-sweep lookahead (current lean for roomscale):** execute the
   existing auxiliary sweep with callbacks disabled solely to choose fresh
   native dispatch before input/QC. Restore command, origin, public support and
   existing pusher-support record, water values and normal links. Only q30 with
   positive horizontal roomscale needs this probe; ordinary dry movement is
   unchanged. `SV_WalkMove` / FlyMove / PushEntityTo's explicit writes appear
   confined to origin/velocity/flags/ground and links; the clip context is
   cleared. [unknown] `SV_LinkEdict` calls weapon-pose and push-grid hooks even
   without triggers; establish whether their effects can be restored/recomputed
   without introducing a second transaction framework. Probe must not alter
   contact/pusher/discontinuity policy or trigger tie ordering.
2. **Input-phase adapter:** refactor the smallest native input calculation so
   that a pre-QC disposition can retain its pre-room origin/water/angle context.
   [unknown] whether this can preserve native Gorilla deferral and instant-stop
   without duplicated phase policy. Running full ClientThink late is rejected.
3. **Keep shared selected solver for an already-started living transition:**
   honor actual QC impulse/zero in the remaining interval, then mark the frame
   native and stop batching. [unknown] acceptable only for transitions whose
   remaining solver contract is demonstrated; wet/ladder/hull changes cannot be
   silently run as ordinary PM_NORMAL. The complete state inventory should make
   ordinary PreThink ability entry impossible except explicit lifecycle or
   scheduled callbacks; prove that claim rather than assuming it.

Rejected: all roomscale commands native (would suppress most VR prediction),
second queued/native solver, QC once-world scheduler, force emulator, replaying
PreThink, and abandoning the full normal-admission contract for a dormant helper.
The phase question and roomscale probe may be the same decision; merge them if
that simplifies ownership. Do not re-review the committed transport/consumer or
the full renderer migration.

## Required review output

One local Astra Max reviewer: verify load-bearing claims in actual code, then
give a prioritized <=1,200-word critique, file/line evidence and chosen minimal
phase contract. Separate demonstrated incompatibilities from uncertainty.
Challenge whether a probe/refactor is necessary, especially deletion or simpler
reuse. Choose the highest-leverage decision for deeper specification. No edits,
commits, branch changes, broad test runs or further agents. If evidence is
missing, identify that bounded gap rather than broadening. Main owns final plan,
production integration and spot-checking. Requested Luna coding is unavailable;
this reviewer is read-only and is not a coding substitution.
