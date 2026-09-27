# Instant teleport occupancy on 2.0

The inherited QuakeSpasm VR server remembers the most recent teleport trigger
per client. An instant trigger can omit `teleport_time`, so its source is
suppressed while the player remains in that source's overlap set. Ordinary
teleports use QuakeC's `teleport_time` until the timestamp expires or changes.

The vkQuake port adds that small latch around its existing `SV_TouchLinks`
callback. vkQuake continues to own trigger enumeration, edict retention,
QuakeC execution, and co-op pickup handling. Source identity is cleared on
observed exit, death, disconnect, map reset, or trigger free as appropriate.
When a recognized teleport callback actually changes a live player's origin,
that same boundary now publishes the existing private movement discontinuity.
Selected clients can reset interpolation and speculative movement from the
authoritative relocated owner snapshot. This does not classify unrelated
QuakeC `setorigin` changes as teleports or add another trigger owner.

| Astra senior review finding | Disposition |
| --- | --- |
| Keep vkQuake's trigger traversal and retention | Adopted; no parallel trigger system |
| Keep distinct slot, map, death, and free invalidation | Adopted; each covers a different lifetime |
| The one-entry latch only suppresses the **most recent** source | Adopted as the accurate contract; an A → B chain may make A eligible again before an observed exit |
| A QuakeC callback could recursively invoke trigger handling before its latch is recorded | Documented inherited limit; no mod-specific evidence yet to justify a new reentrancy protocol |
| `qc_teleport_time` is redundant for a valid ordinary entry | Adopted; removed the boolean, retained timestamp comparison |
| Remove ordinary cooldown or add a trigger registry/generation layer | Deferred/rejected without a demonstrated behavioral need |

Static review and a local build cover integration only. Live acceptance still
needs repeated contact, exit/re-entry, distinct trigger chains, slot and trigger
reuse, death, and normal/co-op teleport behavior with the relevant mods. The
inherited QuakeSpasm VR server behavior is the reference for these checks.
Private-client acceptance also needs a completed move ACK and owner snapshot
after a stock or instant trigger, with no interpolation across the relocation.
