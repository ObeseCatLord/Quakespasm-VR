# Gesture-only melee and steady VR weapon presentation

Status: design disposition accepted; bounded implementation in progress. This supersedes
physical-contact melee expansion and the unfinished Copper server integration.
Only branch 2.0. The user's dirty migration-2.0.md remains untouched.

## Required behavior and existing owners

When immersive melee is enabled and the selected weapon is recognized as melee,
a validated physical swing requests ordinary attack input. Physical attack
triggers do not attack with that weapon. The visible held VR melee weapon keeps
a ready pose rather than playing the native scripted attack animation. Original
QC still runs its attack/think sequence, including delayed damage, sounds,
cooldown, reach and effects. No direct damage or named native-leaf dispatch is
added. Desktop, ranged weapons and mode-off play retain native input/animation.

Reuse the reviewed generic owner in vr_input.c, shared classification in
vr_weapon_schema/calibration, CL_FinishMoveInternal's merge seam and the
observational R_SetupAliasFrame seam. The latter already contains a Copper-only
frame override; replace it with the shared gesture presentation predicate.
Generated held/paired meshes already use their recipe ready frames. Do not
rewrite QC, alias interpolation history, command replay or collision handling.

Verified current incompatibilities: generic identity refuses exact-contact
weapons; the private pose path publishes IMMERSIVE_MELEE, which can suppress
native attacks server-side; the generic merge preserves physical BUTTON_ATTACK;
the existing ready-pose predicate requires Copper capability. These four narrow
boundaries must change. Exact server adapters remain dormant rather than being
copied, expanded or broadly deleted.

## Adapter and implementation stages

1. Disable client admission/publication of physical-contact melee at its shared
   policy boundary. Keep collision-only and private ranged/paired poses usable.
   No new wire bit, scheduler, native outcome or server state is needed.
2. Use the same selected-model profile for gesture admission, physical attack
   suppression and ready-pose selection. Remove exact-contact precedence from
   the gesture owner. Keep explicit melee 0 authoritative and unknown models
   opt-in; reuse existing model conventions without substring inference.
3. During final command construction, clear physical attack input for a selected
   gesture-mode melee weapon, then add at most the pending validated swing pulse.
   Preview must remain nonconsuming. Tracking loss must clear pending gestures
   without reenabling physical trigger attacks. Preserve menu/calibration trigger
   interactions outside gameplay. Merge after all button fields are assembled;
   clear the established +button5 bit only for recognized paired melee. At key
   dispatch, suppress direct attack bindings, preserving the default trigger
   jump binding and UI interactions. No arbitrary binding/alias parser.
   Reuse the existing recognizer independently for each physical hand on known
   paired melee; other weapons use the dominant hand. Simultaneous requests
   coalesce into a normal attack pulse, with native QC deciding the attack hand.
4. Hold only the selected held VR model's render pose. A profile can select its
   ready frame if frame0 is unsuitable; validate frame/pose bounds using the
   renderer's MDL/MD3/MD5 conventions. No entity or server frame mutation, no
   global animation suppression. Existing generated held/paired meshes retain
   their own ready poses and identities.
5. Main reviews/integrates one bounded coding worker's output; local Astra Max
   reviews the design and final source. Consolidated Linux/ARM build/command/
   rendering checks occur after full implementation, as requested.

Expected scope: client policy/merge/read-only pose predicate, two instances of
the same recognizer for paired melee and optional shared profile scalar;
approximately200 integration lines plus profile plumbing. The bilateral adapter
reuses the same state type and native attack owner rather than adding a second
combat scheduler.
Reopen if it needs a second combat scheduler, a wire/protocol change or QC
rewrites. No implementation is authorized merely to preserve postponed exact
physical-contact behavior.

## Deferred acceptance and evidence limits

Actual finalized gesture command requests native attack once; holding/releasing
either trigger on a recognized melee weapon requests none. Cover previews,
retransmission, pending intent aging, tracking/reset/menu/calibration changes,
left-handed mode, mode/profile changes, malformed/out-of-range ready frames,
ordinary desktop/ranged/mode-off input and MDL/MD3/MD5 pose bounds. Verify native
damage/timing/effects alongside held ready-pose output. Ready-pose snapshots and
input bits alone do not certify the whole game. Generic gestures still request
input, not guaranteed damage; mods may reject taps during cooldown or require
held input. No retry-to-damage loop is added. Native QC determines the target
and reach; there is no blade-edge contact or universal parry promise.

## Astra Max design/source disposition

| Finding | Decision |
| --- | --- |
| Physical +button5 arrives after old merge seam | Merge after all physical bits, before calibration suppression. Clear the known paired-melee offhand bit along with primary input. Preview remains nonconsuming. |
| Both trigger keys disabled would lose default jump | Suppress only direct attack bindings in gameplay; preserve jump and UI/calibration trigger ownership. Final command filtering also catches stale key press impulses. |
| Tracking-independent mode predicate omitted controller aim | Use the same controller-aim eligibility as gestures, without requiring current hand tracking or viewentity agreement for physical attack suppression. |
| MD3 ready poses used numposes=1 | Bound PV_QUAKE3 by numframes like MD5; MDL uses numposes. Keep read-only frame0 fallback and validate selected surface chain. |
| Disabling contact authorization also lost collision geometry | Restore geometry eligibility and gate only immersive contact producers plus final admission. Retain the existing collision/pose owners; no server change. |
| Dominant-only recognition loses paired melee swings | Reuse the recognizer per hand for existing paired melee, with coalesced native requests. Share profile enable/disable with generated ready-pose presentation. No direct blade damage or per-hand native leaf invocation. |

Local gpt-6-astra/max verified the adapter approach needs no new layer.
Final combined source review and deferred software qualification remain required.
