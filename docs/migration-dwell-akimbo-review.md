# Dwell paired axes: architecture review and implementation boundary

An Astra source review found that Dwell's berserk axes are a larger vertical
than the Enyo SMGs. The installed Dwell 2.2 program is 820938 bytes with SHA-256
`fe7d21d4bdfd1a5e6672d1606efd774cb730d7f5b68941d82175596f22e719fd`.
Its `W_FireAxe` has first statement 14560 and calls `makevectors` at 14565,
then `traceline2` at 14578 and 14586. The `berserk_finished` field is present.
These facts were checked against the installed program under GDB; the review
also inspected the donor source. No Dwell port was made in this review.

| Recommendation | Disposition |
| --- | --- |
| Keep the existing model loader, private pair command, pose scope and contact transport. | **Adopt.** They remain the narrow owners; do not copy the donor's parallel akimbo context or add another protocol. |
| Enable the Dwell pair after only a pinned `makevectors` hook. | **Reject.** The donor's immersive paired axes also require two source-pinned cutting edges, the Dwell contact profile, QC helper trace filtering, and correct per-hand held transforms. A cosmetic or trigger-only path must be explicitly scoped and qualified before any offer. |
| Reuse the stock-axe one-shot contact handler as-is. | **Reject.** Dwell's `W_FireAxe → traceline2` helper can retry after acquisition; its accepted fraction and later misses need the donor's scoped helper semantics. Calling the native swing scheduler again can duplicate a delayed strike. |
| Apply the generic dominant-hand origin before Dwell QuakeC runs. | **Reject.** The donor defers paired pose mutation until Dwell's pinned strike, preserving earlier QuakeC observations and scope restoration. |
| Implement Dwell next as a single small slice. | **Defer.** Sequence the held/edge transforms, paired contact admission, QC helper/outcome adapter and live parity proof together, then enable the offer. The smaller next user-visible slice is QBJ3 shotgun wrist-roll preservation. |

The installed Dwell desktop map entered gameplay without an OpenXR runtime.
That startup check does not establish firing or immersive melee equivalence.
The Dwell pair capability and melee profile remain off on `2.0` until the
complete admitted behavior is present. Physical headset checks remain with
the user; Windows and ARM qualification remain later release gates.
