# Rooftop audio failure containment

User reports all audio stopping during PC play on Malignant Normality
(`quake_rooftop_jam_v2`, `qrt_naitelveni`). Actual packaged callback probes
produce 2,560 invalid final-mixer samples at startup, but retain a moving clock
and audible output afterward, including a sixty-second map traversal. The exact
late cutoff and its source remain unconfirmed. These observations warrant a
narrow invalid-output boundary repair, not a replacement audio backend.

## Design before implementation

Reuse Steam Audio's existing callback, scene worker, effect allocations,
reflection handoff, dry sources, music queue and final guard. Validate reflection
Apply/GetTail ambisonics before decoding, decoded stereo and voice before
addition, and every candidate dry-plus-wet addition before committing any
sample. Discard only invalid wet branches. Reset only their existing effect
(or the decoder when its output fails). Healthy dry sound, voice and music must
continue. Preserve the silent hybrid handoff in parametric mode.

Report fault blocks through callback-owned `sa_stats_t`, read under the existing
DMA exclusion; the worker's room-statistics lock does not protect callback writes.
No scene/renderer reconstruction, device reset, global mute, new thread, mode
fallback, permanent reverb downgrade or mod-specific workaround.

## Astra senior review disposition

| Recommendation | Disposition |
| --- | --- |
| Transactional finite checks, including finite overflow | Adopt; never contaminate or partially overwrite a valid dry block. |
| Targeted resets and retain healthy branches | Adopt; no reset-all or worker waits on fault. |
| Statistics must use callback owner | Adopt; return fault flags to the existing renderer stats owner. |
| SDK reset does not clear every IIR history | Verified against pinned SDK source; do not claim repair of all internal SDK corruption. Containment remains effective on consecutive failures. |
| Reset cost is not proven merely by allocation-free operation | Adopt a bounded fault-deadline check after implementation; no general performance benchmark. |
| Original late cutoff not reproduced | Adopt; report the observed numerical defect and its proof separately. |

Official reference: [Valve reflection effect API](https://valvesoftware.github.io/steam-audio/doc/capi/reflections-effect.html).
The pinned SDK [reverb reset](https://github.com/ValveSoftware/steam-audio/blob/0da18255cca520771f363ee01f100572b39a308e/core/src/core/reverb_effect.cpp#L59)
does not reset its absorptive/tone IIR histories. An upstream issue is a
plausibility report, not proof of this user's Linux failure.

## Acceptance after implementation

Exercise native SDK effects with injected NaN/infinities in reflection Apply,
tails, decoder and voice; finite-addition overflow; both modes; consecutive
faults; independent healthy branches and actual IR handoff. Assert unchanged
finite dry input on fully rejected wet blocks and finite subsequent output.
Then run actual SDL callbacks on the authored rooftop map, including simulated
OpenXR, and check output/clock/fault statistics. This cannot establish physical
headset audio or identify an unavailable trigger from the reported late event.

## Integration results

Astra model and xhigh effort were independently verified from the review
thread's effective turn context. Main spot-checked the pinned Valve reset source
and corrected the brief's room-statistics locking assumption.

The native SDK boundary probe passes 36 cases / 288 consecutive fault blocks:
reflection Apply/tails, decoded stereo, voice, simultaneous bad branches, and
finite addition overflow in both room modes with NaN and either infinity. Fully
rejected wet blocks preserve the dry input exactly. Subsequent output remains
finite. The maximum observed fault call was 0.508 ms on this PC, below the
5.33 ms block interval; this is a fault-deadline check, not a headset performance
benchmark. Returned-buffer faults do not prove recovery from poisoned internal
SDK IIR state; containment protects dry output even if invalid wet persists.

## Binaural input prevention and output containment follow-up

Main's post-room-fix native rooftop probe reports `room_nonfinite_blocks = 0`
while the final mixer still reports 2,560 nonfinite samples. This observation
does not support attributing that startup defect to room wet output. A native
GDB conditional breakpoint immediately before binaural Apply hit with all three
direction components zero. The subsequent native metadata probe repeatedly
confirms positional source 132 at distance 289.99 and gain 0.0133, with listener
origin zero, a nonzero timestamp, and all three listener basis vectors zero.
The exact late PC all-audio cutoff and its cause remain unconfirmed.

The [Valve binaural effect API](https://valvesoftware.github.io/steam-audio/doc/capi/binaural-effect.html)
requires `direction` to be a unit vector from listener to source. Although
`SA_Create` initializes a listener basis, `Spatial_Listener` can publish a zero
basis during startup/invalidation. Validate finite origin and finite,
nondegenerate forward/right/up vectors within the existing `SA_SetListener`
owner before publication. Reject the entire invalid pose without replacing the
previous origin, basis or timestamp. Retain the already-owned last valid pose,
or the canonical initialized pose before the first valid sample; initialize the
existing render snapshot from that canonical pose as well. Settings publication
then forwards only the retained valid listener to room simulation, and the
callback uses it for room decode orientation. No extra pose cache or tracking
policy is needed. Still normalize the projected direction at the SDK boundary,
including for finite scaled bases. For a nonfinite or degenerate length, use
the existing SDK forward `(0, 0, -1)`.

After binaural Apply or GetTail, validate both complete stereo channels before
source mixing. If either contains a nonfinite value, reset only that source's
existing binaural effect, clear its tail, and replace both output channels with
finite input mono for that block. Ensure the failed HRTF arrays cannot enter a
lerp, including when their coefficient is zero. Retain existing gain ramps,
source clocks, room sends, radio processing, healthy sources and dry music.
Record `binaural_nonfinite_blocks` once per affected callback block in the
existing callback-owned stats, read through the existing DMA exclusion.

No renderer reconstruction, new state machine, worker/thread, allocation,
backend change or SDK-internal recovery claim. Main owns private SDK/source
fixtures and GDB/end-to-end validation: cover zero/non-unit/nonfinite projected
directions; rejected zero/individual-degenerate/nonfinite bases and nonfinite
origins retaining the previous timestamp; accepted finite scaled bases;
invalid Apply/tail stereo on either channel including late samples,
HRTF-off and spatial-blend-zero paths, concurrent healthy sources and music,
consecutive faults, source-tail progress and callback fault counters. Builds and
runtime tests are held until handoff and main integration.

## Final Astra review disposition

The final Astra xhigh pass retained the existing architecture and identified
finite binaural samples overflowing after gain and accumulation. Adopted:
stage each complete source mix before committing; on failure reset that source
and retry finite mono; reject a still-invalid fallback without touching healthy
mix. Native fault probes must include this analytical counterexample. Adopted
the simplification of deleting the redundant outer room transaction: each
existing `add_wet` already commits a complete validated branch. Adapted wording:
listener validation rejects nonfinite/near-zero individual axes; it does not
claim orthogonality validation for nonzero malformed frames not observed here.

Final integration: the full native mixer passes 81 injected binaural failures,
including tail, HRTF-off/dry and finite-gain overflow, retaining an independent
healthy sound plus dry music and zero callback allocations. The simplified
room transaction still passes all 288 faults. Actual PC desktop and simulated
OpenXR rooftop callbacks now report zero final invalid samples with advancing
clock and output; the simulated run also executes private normal movement
commands and exits normally. The original late physical cutoff is unconfirmed.
