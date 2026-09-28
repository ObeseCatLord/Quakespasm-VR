# q30 living callback phase decision

Mostly-worked design brief after `adaf628a`, for the remaining phase stages of
[the committed transition plan](predictive-q30-transitions-2.0-plan.md). Solo
engine, no new scheduling or force-emulation framework. Full normal admission
remains required; admission is still closed while this boundary is unresolved.

| Environment | Verified source/evidence |
| --- | --- |
| Workspace | `quakespasm-2.0` on `2.0`; do not touch main or user-dirty `docs/migration-2.0.md`. |
| Exact program | Installed q30 SHA256 `5e69fece92fb4323609c8e1209a39eecf4f70c3161ae17beb53063fe3e06c340`; source-like `/tmp/q30-source-check/client.qc` is explanatory, not certified source. |
| Production checkpoint | Typed current-state classifier, dispatch-only roomscale/water probe, held hand/contact exclusion and after-movement native batching fence. |
| References and owners | Shared PMove owns ordinary selected movement; native input/QC/physics remains in `sv_user.c` and `sv_phys.c`. One queue, credit, completion cursor and scheduled Think window. |
| Verification | Exact-program QC fixtures and real BSP; component seams explicitly identified. Linux build now. Live/headset, performance, Windows/ARM deferred. |

## Evidence versus inference

- [verified: `sv_phys.c:8077–8145`] The existing enum distinguishes fresh,
  after-PreThink and after-weapon-Think phases. Terminal/stock-freeze continuation
  drains the head's contact, resets hands, restores **world duration** and calls
  the existing native phase owner. The selected tail commits the head once.
  Extending this unchanged to a living q30 command can consume the wrong time.
- [verified: `sv_phys.c:8190–8580`] Selected execution performs roomscale,
  fresh water, stop, PreThink, scheduled Think and then shared PMove. A callback
  switching to a valid living native state currently hits strict StateError;
  only terminal/stock freeze has a residual continuation. Maintenance has the
  same gap, but no new movement interval/input is owed.
- [verified: `sv_user.c:614–635`] Native ClientThink performs angle/recoil and
  acceleration **before** native roomscale/PreThink, or defers eligible hands.
  Running ClientThink after an authored zero/velocity change is not parity.
- [verified: `sv_phys.c:9183–9397`] NativeFromPhase already skips prior QC,
  uses the existing Think opportunity, native movement, contacts and PostThink.
  AFTER_WEAPON_THINK retains pre-Think WALK dispatch, matching native type
  capture; configured Gorilla may reselect. Its after-PreThink hand path can
  resume deferred input, and both residual entries use native move-frame capture
  at entry, not the original pre-QC capture. These need explicit disposition.
- [verified: current q30 classifier] Startup, existing boots/grapple eligibility,
  SSG, ladder, pending gravity normalization, holds/cameras, wet/waterjump,
  nonempty target2 and changed hull/mode already choose fresh native before QC.
  Existing ability activation therefore must not be mistaken for ordinary entry.
- [verified: exact-program prior checkpoint / source-like cross-check] Ordinary
  PreThink includes CheckRules, WaterMove, jump and hold; PostThink invokes
  targets and W_WeaponFrame. Ordinary debuffs can kill; world-inflictor damage
  does not enter the knockback branch. Scheduled player Think is weapon animation
  or authored callback. [unknown] Complete closure of all reachable ordinary
  PreThink/scheduled Think paths has not yet been proved. Source aliases are not
  authority for constants.
- [verified: previous review] A callback may invalidate an accepted hand sample
  by relocation; resetting its cursor must preserve the greater existing cutoff.
  Replaying hand contacts/weapon input after that cutoff is not permitted.

## Candidate minimal contract and open decisions

1. **Living residual native physics at command duration (current lean, subject
   to closure proof).** At the existing after-PreThink/after-Think boundary,
   validate the current body, mark the current interval native, preserve QC's
   state, cancel deferred input and fence this head's hand/contact continuity,
   then reuse NativeFromPhase with **this command duration**. No late ClientThink,
   repeated roomscale, repeated QC or appended world interval. Complete this
   head in the existing tail and retain later heads. Maintenance uses zero
   duration and completed input only. Existing stock terminal/world-time behavior
   stays unchanged. [unknown] Omitting analog acceleration for this transitional
   interval is acceptable only where reachable QC overwrites it or a specifically
   verified residual contract establishes the intended selected behavior. Do
   not declare broad parity from a no-input injected test.
2. **Keep shared solver for demonstrably compatible transient state.** A field
   eligibility change without new force/hull/water can finish selected movement
   and merely fence replay/batching. [unknown] This could be smaller than any
   native phase expansion, but must not run PM_NORMAL on an incompatible mode,
   ignore a hold or add input after a forced zero.
3. **Pre-QC native input adapter.** Reuse existing input calculation before QC
   and reconcile it for ordinary PMove. This would touch every ordinary command,
   complicate jump/stop/Gorilla ordering and potentially duplicate input policy.
   Consider only for a demonstrated reachable incompatibility; uncertainty alone
   is not permission to add a shadow body/force journal/second input owner.

Rejected: arbitrary living states through unchanged terminal world continuation,
full late ClientThink, callback replay, all q30 commands native, new pending-state
flags/scheduler, and suppressing ordinary admission forever as feature completion.
Type capture, duration and hand continuation may be one phase contract; merge or
delete unnecessary helpers. Main will independently audit reachable QC while
the review focuses on these existing-owner integration decisions.

## Required review

One local Astra Max, read-only, verify before critique. Scope: current
`sv_phys.c` selected/native residual boundaries and `sv_user.c` native/deferred
input; the linked current plan/review provide context. Return <=1,200 words with
prioritized source-evidenced findings, the smallest defensible remaining-phase
contract, and the exact evidence still needed before normal admission. Challenge
the necessity of every new abstraction; no force emulator or second scheduler.
Do not edit, commit, change branches, run broad tests or spawn agents. Do not
redo policy/codec, renderer/foveation or the unrelated stock10ms assertion.
Main owns QC closure investigation, plan disposition, coding and software checks.
