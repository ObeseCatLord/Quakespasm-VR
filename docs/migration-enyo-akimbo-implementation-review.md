# Enyo paired-SMG integration review

An Astra xhigh senior review inspected the combined Enyo adapter after the
initial client-to-server path ran under simulated Monado tracking. The review
endorsed the narrow, hash-pinned QuakeC hooks and reuse of the existing private
pose scope. It did not treat reaching the clearance trace as proof of bullet
impact, damage, or both alternating hands.

| Review finding | Disposition |
| --- | --- |
| MDL frame-header allocation and skin-size arithmetic could overflow before later guards. | **Adopted.** Check the MDL header length, frame count and allocation size before use; check skin dimensions before existing `int` consumers. The installed-model smoke is part of qualification. |
| MD5 weight allocation and `firstweight + count` could overflow. | **Adopted.** Bound the allocation multiplication and validate influence ranges using subtraction. |
| MD3 source offsets and spans are still trusted even though destination allocation sizes are checked. | **Tracked separately.** This is an inherited source-buffer validation gap, not evidence that the new destination checks are wrong. Do not claim full untrusted-model parser hardening. |
| A failed two-hand collision solve still admitted raw pair poses. | **Adopted.** An unresolved hand now drops the entire pair for that frame in presentation and command preparation, leaving the ordinary viewmodel/command path available. Tight-corner visual and gameplay qualification remains open. |
| The first Enyo probe stopped before `FireBullets2` and proved only one firing path. | **Adopted.** Keep impact/damage, both hands, obstruction, callbacks, and desktop firing equivalence as open acceptance checks. Native QC consumed one nail in the simulated first-shot run, but that is not a damage test. |
| Pair collision allowlists and Enyo clearance flags duplicated policy/state. | **Adopted.** Share one client-side recipe predicate and keep one one-shot clearance flag; server QC authorization remains independent. |

The desktop render path clears the pair and does not require an OpenXR runtime.
An Enyo desktop map entered gameplay with an invalid runtime path; this is a
startup smoke, not full rendering or firing equivalence. The QBJ3 native QC
regression still covers both VR hands, cadence, damage metadata, scope restore,
and ordinary fallback. The Enyo simulated OpenXR path currently reaches both
drawn halves, distinct wire muzzles, the server scope, and the pinned
`makevectors`/`aim`/clearance hooks. Physical-headset evaluation remains with
the user; Windows and ARM qualification are later release gates.
