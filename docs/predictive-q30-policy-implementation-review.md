# Ordinary q30 replay transport and consumer checkpoint

The [feature plan](predictive-q30-replay-2.0-plan.md) was committed as
`5a6e33fe` before production work. The implementation review's numeric/encoded
seed boundary revision was committed as `9eea9646`. This checkpoint retains the
existing movement, command, snapshot and protocol owners; it does not activate
q30 or complete the parent AD-family migration.

## Senior review disposition

Local Astra Max reviewed the actual production edits, read-only. Main verified
effective `gpt-6-astra` / `max` settings from the local turn metadata, including
both final review turns. The reviewer could not see those effective settings from
its own tools. Main independently spot-checked the load-bearing source claims.

| Finding | Disposition and evidence |
| --- | --- |
| Roomscale can snap a low airborne takeoff if QC rising exclusion is disabled before its categorization. | **Adopted.** Keep QC geometry during the existing roomscale helper; use ordinary categorization only for the disposable stop probe. Real QC/server vs replay matches a 120-height 5 ms takeoff followed by release and repeated horizontal roomscale close to the floor. |
| NaN/infinite/large floating stats undergo an undefined integer conversion before candidate validation. | **Adopted.** Extend the existing timer integer-companion guard to height250 and limit223, preserving original float inputs. Address/UB/explicit float-cast-overflow checks reject missing/stale/negative/nonfinite limits and preserve large finite height/limit floats with defined companions. |
| A finite velocity limit does not make every authoritative velocity representable in the existing signed-short ACK. | **Adopted.** Define encoding and withhold private replay outside −4096 through 4095.875. Actual writer/full-parser/replay checks qualify both endpoints and refuse outside seeds without changing authored physics. Preserve the wire layout and existing in-range truncation. |
| The live-height change refreshes movevars but happens after the re-jump, so that takeoff does not consume the changed height. | **Adopted.** Move the change before the released, supported press; assert that precondition, then compare actual QC and replay through that takeoff. |

The final scoped review found no blocking defect in this transport/consumer
stage and accepted the three corrections and local reach estimate. It verified
the recorded native/snapshot/mixed/preview pass markers but did not rerun the
checks; main owns the reported build and sanitizer evidence. The small
changed-height coverage correction was applied after that review and rerun.

The existing collision collector estimates bounded reach from the actual
pre-solver velocity clamp for this policy, without replacing the authored
height. This prevents an authored height of `1e30` with limit `2000` from widening
ordinary collection to the whole map; the real-QC comparison verifies matching
movement and a bounded estimate. It is correctness/domain evidence, not a
performance measurement.

## Software evidence and remaining contract

Consolidated Linux/SDL3 warnings-as-errors build passes. Seven exact-q30
comparison cases match server QC + selected PMove against the real replay
consumer, with axis differences below .01 and identical release/ground flags.
The [existing fixtures](../tests/README.md#exact-q30-movement-comparison) also
cover actual capability handling, the loaded registry's nine disjoint custom
stats, staged slot/width collisions and full serialized stats/owner → production
parser → movevars. That injected owner remains unpredicted as required.

Real solver repeated/zero previews preserve the command journal, pending input
and authoritative entity, including no second held impulse after a committed
jump. Stock/public mixed checks retain actual receipt, QC, physics, parser and
replay, including startup pause, arrival gaps and native return. The fixture
seams and sanitizer environment are recorded in the feature plan and README;
they are not live tracking or connected signon evidence.

Ordinary q30 admission remains closed. Shared startup/ability/hold/camera and
phase-aware transitions, raw Gorilla, authored step/ledge/trigger traversal and
normal offer/spawn/begin/replay remain required before activation. Wider
AD/Mjolnir and cooperative-QC support remain in the parent plan. Device/eye
tests, performance measurement and Windows/ARM qualification remain deferred.
