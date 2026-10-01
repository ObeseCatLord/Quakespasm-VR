# Inherited controller axis and Vive click parity

2026-09-30. Before-code adopted plan after bounded source design review.
Builds/tests remain deferred until all surviving migration implementation ends.

## Verified behavior and gap

Primary `51b452c018273647dcf94f4628a370267ff8fa91:Quake/vr.c:11651`
emits weapon-hand horizontal LEFTARROW/RIGHTARROW key edges during gameplay,
using the inherited menu-extra deadzone. Lines11768–11783 additionally enqueue
impulse10/12 on a rising Vive weapon-hand pad click whose filtered X exceeds
positive/negative0.3. Pad click still emits RTHUMB; cycling supplements it.
The same primary routes weapon-hand vertical input to the wheel keys.

Current `vr_openxr.cpp:input_for_hand` supplies profile, pad click and axes.
`vr_input.c:VR_InputBuildHandDesired` preserves click and vertical keys but only
emits horizontal arrows from the menu offhand. Searches of current vr*.c and
view.c found no replacement Vive sector-cycle handler. Primary's generic
GetAxis strongest-axis/filter policy already exists in VR_InputFilteredAxis.
Native Key_Event/Cbuf/IN_Impulse remain the command owners; do not write usercmd
or transport fields. Hand roles are logical, so left-handed play works too.

## Adopted minimal adapter

Restore gameplay weapon-hand horizontal edges through VR_InputAddAxis and the
existing desired/aggregate key ownership. Keep menu offhand navigation and
weapon-hand vertical wheel input. Preserve native binding behavior, including
user-bound commands, rather than adding a second custom-binding dispatcher.

For Vive cycling, use a per-call impulse candidate detected only for the
accepted logical weapon hand: desired RTHUMB true and its previous per-hand
owned RTHUMB false, Vive profile, key_game, no binding capture/modal grab.
Compute sector from the existing filtered X with extra0; +X>0.3 chooses10,
-X<-0.3 chooses12. Do not store a second persistent pressed mask. Queue once
only after VR_InputEmitDesired succeeds, provided its context/dispatch is still
valid. Snapshot wheel/calibration consumption at Commands entry before its
existing cancellation or completion can retire calibration, then also check
live consumption before enqueueing. Retain the composed pad-click RTHUMB key.
Accepted neutral/profile/focus
gates already prevent held-input leakage on entry or reassignment. Moving
between sectors during a held click must not generate another impulse.

Per-hand RTHUMB ownership is sufficient for the accepted Vive weapon hand:
there is no competing builder producer for that key. Suppressed or center-click
edges still establish ordinary desired ownership, so closing an interface or
moving to a sector while held cannot release a delayed cycle. Do not add a
persistent edge bool, pressed mask or sample marker. Modal grabs and binding
capture never inject gameplay impulses. If key dispatch fails or changes
context/epoch, discard this call's candidate permanently.

Consumption suppression covers already-active or synchronously changed wheel/
calibration state. Native Key_Event queues bindings for later Cbuf execution;
a same-sample wheel-open binding can therefore precede the supplemental cycle
in the queue. Retain primary's ordinary-binding-before-cycle ordering rather
than parsing binding strings or inventing execution-time exclusion policy.

Expected production scope: Quake/vr_input.c only, existing axis builder and
Commands/EmitDesired boundary. No renderer, backend profile, native key table,
binding-default, locomotion, prediction, protocol or server rewrite.

## Deferred qualification

After implementation ends, exercise actual backend samples through key events
and native command execution, not direct kbutton/usercmd writes: logical hand
swap, Vive +/-/center rising pad clicks, held click/sector changes/release,
non-Vive clicks, snap/smooth turning and custom horizontal bindings. Cover
duplicate samples, both hands contributing the same key, focus/profile/role
changes and neutral rearm; menu, modal cancel, capture, wheel and calibration
transitions including Key_Event callbacks that change context. Verify paused
private impulses follow the existing pause adapter and native desktop input is
unchanged. Source review alone does not qualify physical controller behavior.

## Before-code review disposition

Requested local Astra xhigh verified the primary omissions and actual current
owner chain. Main adopts the bounded advice below; effective routing metadata
is unavailable, so this is source advice, not certified final-goal signoff.

| Recommendation | Main disposition |
| --- | --- |
| Restore gameplay horizontal edges at AddAxis | Adopt: reuse native filtering and desired aggregate ownership. |
| Use prior per-hand RTHUMB ownership for Vive pad edge | Adopt: one accepted producer; successful emission records aggregate-equal contributions too. |
| Add another persistent press/sample state | Reject: existing ownership and neutral gates already carry the required lifetime. |
| Require successful emission before queueing | Adopt: callback reset/epoch/context checks must decide the candidate commit. |
| Protect calibration consumption that can end within Commands | Adopt correction: snapshot at entry and check live state after dispatch. |
| Claim same-sample queued wheel-open execution exclusion | Reject: preserve inherited native queue ordering; no binding parser/new dispatcher. |

Review was source-only, with no tests/builds/compiler/probes or device execution.

## Actual production source receipt

Commit786ca872 changes only vr_input.c (23 added lines/one replacement).
Luna implemented the specified adapter. Main inspected the entire diff and
relevant producer/consumer gates, retaining native qboolean for the local
snapshot. Requested local Astra xhigh rechecked the actual committed patch
with git show and reported no P1/P2 findings, recommending bounded acceptance.
Effective routing remains unverified; this is source advice, not formal final
goal certification.

The entry snapshot precedes calibration cancellation/completion. Accepted-hand
desired/previous ownership selects at most one per-call candidate; centered or
consumed clicks still record ownership. Successful EmitDesired and current
dispatch/context/live-consumption checks govern the native queue append.
Existing trigger-release cleanup still runs even when emission fails. The
horizontal mapping shares native desired-key release and binding semantics.
Scoped whitespace checks passed; no software/runtime tests or builds ran.
All deferred qualification above remains pending.
